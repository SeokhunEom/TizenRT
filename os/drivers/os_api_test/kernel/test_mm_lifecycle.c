/* SPDX-License-Identifier: Apache-2.0 */
#include <tinyara/config.h>
#include <tinyara/binfmt/binfmt.h>
#include <tinyara/binary_manager.h>
#include <tinyara/irq.h>
#include <tinyara/mm/mm.h>
#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>
#include <errno.h>
#include <queue.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "binary_manager/binary_manager_internal.h"
#include "sched/sched.h"

#define LIFE_APP 2
#define LIFE_ROUNDS 3
#define LIFE_POLLS 1000
#define LIFE_DELAY_US 20000

extern struct binary_s *g_lib_binp;
extern sq_queue_t g_sem_list;

struct app_observation_s {
	unsigned main_sequence;
	unsigned child_sequence;
	unsigned group_sequence;
	unsigned failed;
	pid_t main_pid;
	pid_t child_pid;
	pid_t group_pid;
	uintptr_t main_group;
	struct mm_heap_s *heap;
	uintptr_t start;
	uintptr_t end;
};

static struct app_observation_s g_app;
static bool g_running;

static bool report_heap_matches(const struct mm_lifecycle_app_report_s *report,
	struct mm_heap_s *heap, unsigned int bytes)
{
	return report->live_bytes == bytes && report->live &&
		(uintptr_t)report->live <= UINTPTR_MAX - (bytes - 1) &&
		mm_get_heap(report->live) == heap &&
		mm_get_heap((void *)((uintptr_t)report->live + bytes - 1)) == heap;
}

static struct app_observation_s app_snapshot(void)
{
	irqstate_t flags = irqsave();
	struct app_observation_s result = g_app;
	irqrestore(flags);
	return result;
}

static int app_report(unsigned long arg)
{
	const struct mm_lifecycle_app_report_s *report = (const void *)arg;
	struct tcb_s *self = sched_self();
	struct binary_s *bin = BIN_LOADINFO(LIFE_APP);
	irqstate_t flags;
	if (!report || !self || self->app_id != LIFE_APP || !bin || !bin->uheap) { return -EINVAL; }
	flags = irqsave();
	if (report->stage == 1) {
		g_app.main_sequence++;
		g_app.main_pid = self->pid;
		g_app.main_group = (uintptr_t)self->group;
		g_app.heap = bin->uheap;
		g_app.start = bin->ramstart;
		g_app.end = bin->ramstart + bin->ramsize;
		/* Check both endpoints against the kernel's active app-heap registry.
		 * A successful user malloc alone does not prove app2 owns its arena.
		 */
		g_app.failed = report->status != 0 || !report_heap_matches(report, bin->uheap, 257);
	} else if (report->stage == 2 && g_app.main_pid != self->pid &&
			sched_gettcb(g_app.main_pid) == NULL && g_app.heap == bin->uheap &&
			g_app.main_group == (uintptr_t)self->group) {
		g_app.child_sequence++;
		g_app.child_pid = self->pid;
		g_app.failed |= report->status != 0 || !report_heap_matches(report, bin->uheap, 521);
	} else if (report->stage == 3 && g_app.main_pid != self->pid && g_app.child_pid != self->pid &&
			g_app.child_sequence == g_app.main_sequence &&
			sched_gettcb(g_app.main_pid) == NULL && sched_gettcb(g_app.child_pid) == NULL &&
			g_app.heap == bin->uheap && g_app.main_group != (uintptr_t)self->group) {
		g_app.group_sequence++;
		g_app.group_pid = self->pid;
		g_app.failed |= report->status != 0 || !report_heap_matches(report, bin->uheap, 733);
	} else {
		irqrestore(flags);
		return -EINVAL;
	}
	irqrestore(flags);
	return OK;
}

static bool queue_has_range(const volatile sq_queue_t *queue, uintptr_t start, uintptr_t end)
{
	const sq_entry_t *entry = queue->head;
	unsigned limit = 100000;
	while (entry && limit--) {
		uintptr_t address = (uintptr_t)entry;
		if (address >= start && address < end) { return true; }
		entry = entry->flink;
	}
	return entry != NULL;
}

static bool removed_from_registries(const struct app_observation_s *app)
{
	bool clean;
	uint32_t *heap_table;
	irqstate_t flags;
	if (!app->heap || app->end <= app->start || !g_lib_binp) { return false; }
	if (mm_app_heap_registered(app->heap)) { return false; }
	flags = irqsave();
	heap_table = (uint32_t *)(g_lib_binp->sections[BIN_DATA] + 8);
	clean = heap_table[LIFE_APP] == 0 &&
		!queue_has_range(&g_delayed_kufree, app->start, app->end) &&
		!queue_has_range(&g_delayed_kfree, app->start, app->end) &&
		!queue_has_range(&g_sem_list, app->start, app->end) &&
		sched_gettcb(app->main_pid) == NULL && sched_gettcb(app->child_pid) == NULL &&
		sched_gettcb(app->group_pid) == NULL;
	irqrestore(flags);
	return clean;
}

static bool heap_snapshot(struct mallinfo info[CONFIG_KMM_NHEAPS])
{
	struct mm_heap_s *heaps = kmm_get_baseheap();
	int i;
	for (i = 0; i < CONFIG_KMM_NHEAPS; i++) {
		if (mm_mallinfo(&heaps[i], &info[i]) != OK) { return false; }
	}
	return true;
}

static bool same_heaps(const struct mallinfo *a, const struct mallinfo *b)
{
	int i;
	for (i = 0; i < CONFIG_KMM_NHEAPS; i++) {
		if (a[i].arena != b[i].arena || a[i].uordblks != b[i].uordblks ||
			a[i].fordblks != b[i].fordblks) { return false; }
	}
	return true;
}

static int lifecycle_run(void)
{
	struct mallinfo baseline[CONFIG_KMM_NHEAPS];
	struct mallinfo current[CONFIG_KMM_NHEAPS];
	struct mm_lifecycle_stats_s before;
	struct mm_lifecycle_stats_s after;
	struct app_observation_s app;
	unsigned sequence;
	unsigned child_sequence;
	unsigned group_sequence;
	irqstate_t flags;
	int cancel_state;
	int round;
	int poll;
	int ret = -EIO;
	if (task_setcancelstate(TASK_CANCEL_DISABLE, &cancel_state) != OK) { return -EIO; }
	flags = irqsave();
	if (g_running) {
		irqrestore(flags);
		task_setcancelstate(cancel_state, NULL);
		return -EBUSY;
	}
	g_running = true;
	irqrestore(flags);
	printf("NXMM LIFE BEGIN backend=%s revision=%s cycles=3\n",
		mm_nuttx_backend_version(), mm_nuttx_backend_revision());
	/* The boot-time app2 run is a warm-up, not one of the three reloads. */
	for (poll = 0; poll < LIFE_POLLS; poll++) {
		app = app_snapshot();
		if (app.main_sequence && app.child_sequence == app.main_sequence && app.group_sequence == app.main_sequence &&
			BIN_STATE(LIFE_APP) == BINARY_INACTIVE && removed_from_registries(&app)) { break; }
		usleep(LIFE_DELAY_US);
	}
	if (poll == LIFE_POLLS || app.failed) { goto out; }
	usleep(200000);
	if (!heap_snapshot(baseline)) { goto out; }
	for (round = 1; round <= LIFE_ROUNDS; round++) {
		sequence = app.main_sequence;
		child_sequence = app.child_sequence;
		group_sequence = app.group_sequence;
		mm_lifecycle_snapshot(&before);
		printf("NXMM LIFE ROUND BEGIN round=%d\n", round);
		if (binary_manager_execute_loader(LOADCMD_LOAD, LIFE_APP) != OK) { goto out; }
		for (poll = 0; poll < LIFE_POLLS; poll++) {
			app = app_snapshot();
			mm_lifecycle_snapshot(&after);
			if (app.main_sequence == sequence + 1 && app.child_sequence == child_sequence + 1 &&
				app.group_sequence == group_sequence + 1 &&
				BIN_STATE(LIFE_APP) == BINARY_INACTIVE && removed_from_registries(&app) &&
				heap_snapshot(current) && same_heaps(baseline, current)) { break; }
			usleep(LIFE_DELAY_US);
		}
		if (poll == LIFE_POLLS || app.failed || after.initialized != before.initialized + 1 ||
			after.uninitialized != before.uninitialized + 1 || after.last_uninitialized != app.heap) {
			printf("NXMM LIFE CYCLE FAIL round=%d polls=%d main=%u child=%u group=%u init=%u destroy=%u\n",
				round, poll, app.main_sequence - sequence, app.child_sequence - child_sequence,
				app.group_sequence - group_sequence,
				after.initialized - before.initialized, after.uninitialized - before.uninitialized);
			goto out;
		}
		printf("NXMM LIFE CYCLE PASS round=%d main_pid=%d child_pid=%d group_pid=%d heap=%p ownership=1 init=1 destroy=1 registry=0 common=0 delayed=0 heap_restored=1\n",
			round, app.main_pid, app.child_pid, app.group_pid, app.heap);
	}
	ret = OK;
out:
	flags = irqsave();
	g_running = false;
	irqrestore(flags);
	task_setcancelstate(cancel_state, NULL);
	return ret;
}

int test_mm_lifecycle(int cmd, unsigned long arg)
{
	if (cmd == TESTIOC_MM_LIFECYCLE_REPORT) { return app_report(arg); }
	if (cmd == TESTIOC_MM_LIFECYCLE_RUN) { return lifecycle_run(); }
	return -EINVAL;
}
