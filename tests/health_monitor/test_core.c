/* Deterministic scheduling at individual production word-access boundaries.
 * This adapter is a single-thread interleaving model, NOT an ARM memory model
 * or a ThreadSanitizer proof. ARM instructions are checked separately.
 */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define __KERNEL_HEALTH_MONITOR_ACCESS_H
static uint32_t health_load(const uint32_t *address);
static void health_store(uint32_t *address, uint32_t value);
static void health_barrier(void);
static void test_panic(void);
#define PANIC() test_panic()
#include "../../os/kernel/health_monitor/health_monitor.c"

static struct tcb_s current;
static unsigned int masked;
static bool in_irq;
static unsigned int cpu;
static jmp_buf panic_env;
static char output[32768];
static size_t output_len;
static unsigned int panics;
static unsigned int accesses;
static unsigned int source_reads;
static unsigned int inject_at;
static bool inject_enabled;
static void (*read_hook)(const uint32_t *);
static void (*store_hook)(uint32_t *);

irqstate_t irqsave(void) { unsigned int old = masked; masked = 1; return old; }
void irqrestore(irqstate_t old) { masked = old; }
bool up_interrupt_context(void) { return in_irq; }
int up_cpu_index(void) { return cpu; }
struct tcb_s *sched_self(void) { assert(masked); return &current; }
int up_putc(int c) { assert(masked && cpu == 0); assert(output_len + 1 < sizeof(output)); output[output_len++] = c; output[output_len] = 0; return c; }
void sched_foreach(void (*handler)(struct tcb_s *, void *), void *arg)
{
	assert(strstr(output, "HM fault=") == output);
	handler(&current, arg);
}
static void test_panic(void) { panics++; longjmp(panic_env, 1); }
static void tick(void)
{
	unsigned int old_cpu = cpu, old_mask = masked;
	bool old_irq = in_irq;
	cpu = 0; masked = 1; in_irq = true;
	health_monitor_tick();
	cpu = old_cpu; masked = old_mask; in_irq = old_irq;
}
static void access_event(void)
{
	accesses++;
	if (inject_enabled && accesses == inject_at) {
		inject_enabled = false;
		tick();
	}
}
static uint32_t health_load(const uint32_t *address)
{
	uint32_t value = *address;
	if ((uintptr_t)address >= (uintptr_t)g_sources &&
		(uintptr_t)address < (uintptr_t)(g_sources + CONFIG_MAX_TASKS)) source_reads++;
	if (read_hook) read_hook(address);
	access_event();
	return value;
}
static void health_store(uint32_t *address, uint32_t value)
{
	*address = value;
	if (store_hook) store_hook(address);
	access_event();
}
static void health_barrier(void) { access_event(); }
static void reset(void)
{
	memset(g_sources, 0, sizeof(g_sources));
	memset(g_channels, 0, sizeof(g_channels));
	memset(g_cache, 0, sizeof(g_cache));
	memset(g_heap, 0, sizeof(g_heap));
	memset(g_progress_seen, 0, sizeof(g_progress_seen));
	memset(g_progress_epoch_seen, 0, sizeof(g_progress_epoch_seen));
	memset(g_progress_since, 0, sizeof(g_progress_since));
	g_time_seq = g_time_low = g_time_high = g_shutdown = 0;
	g_count = g_fault = 0; g_now = 0;
	g_fault_pid = 0; g_fault_deadline = 0;
	memset(&current, 0, sizeof(current)); current.pid = 1;
	masked = in_irq = output_len = panics = cpu = 0; output[0] = 0;
	accesses = source_reads = inject_at = 0; inject_enabled = false;
	read_hook = NULL; store_hook = NULL;
}
static void set_time(uint64_t now)
{
	g_now = now; g_time_low = (uint32_t)now; g_time_high = now >> 32;
}
static void expect_fatal(unsigned int reason, pid_t pid)
{
	if (!setjmp(panic_env)) { tick(); assert(!"fatal must not return"); }
	assert(panics == 1 && g_fault == reason && g_fault_pid == pid);
	assert(strstr(output, "HM task pid="));
	in_irq = false; masked = 0; cpu = 0;
	assert(health_monitor_kick() == -ESHUTDOWN);
	assert(health_monitor_stop() == -ESHUTDOWN);
	assert(health_monitor_start(10) == -ESHUTDOWN);
}
static void invariants(void)
{
	unsigned int i, count = 0;
	for (i = 0; i < CONFIG_MAX_TASKS; i++) if (g_cache[i].active) count++;
	assert(count == g_count);
	for (i = 0; i < g_count; i++) {
		struct health_cache_s *cache = &g_cache[g_heap[i].slot];
		assert(cache->active && cache->position == i);
		if (i) assert(g_heap[(i - 1) / 2].deadline <= g_heap[i].deadline);
	}
}
static void kick_during_read(const uint32_t *address)
{
	if (address == &g_sources[1].deadline_low) {
		struct health_snapshot_s value;
		read_hook = NULL;
		assert(health_snapshot(1, &value));
		value.deadline += 5;
		health_publish(1, &value, true);
	}
}
static void continually_renew(const uint32_t *address)
{
	if (address == &g_sources[1].deadline_low) {
		struct health_snapshot_s value;
		read_hook = NULL;
		assert(health_snapshot(1, &value));
		value.deadline = g_now + 100;
		health_publish(1, &value, true);
		read_hook = continually_renew;
	}
}
#ifdef TEST_SMP
static void freeze_after_odd(uint32_t *address)
{
	if (address == &g_sources[1].seq && (*address & 1)) {
		store_hook = NULL;
		tick(); tick(); tick(); /* CPU0 never waits on a frozen CPU1 writer. */
	}
}
#endif
int main(void)
{
	unsigned int i;
	struct health_snapshot_s snapshot;
	uint64_t now;
	reset();
	assert(health_monitor_start(0) == -EINVAL);
	assert(health_monitor_kick() == -ENOENT);
	assert(health_monitor_stop() == -ENOENT);
	in_irq = true;
	assert(health_monitor_start(1) == -EPERM);
	assert(health_monitor_kick() == -EPERM);
	assert(health_monitor_stop() == -EPERM);
	in_irq = false; current.pid = 0;
	assert(health_monitor_start(1) == -EINVAL);
	current.pid = 1;
	assert(!health_monitor_start(11));
	assert(!g_count); /* CPU0 alone inserts nodes */
	assert(health_monitor_start(100) == -EALREADY);
	tick(); assert(g_now == 1 && !g_fault && g_count == 1);
	expect_fatal(HEALTH_TIMEOUT, 1);

	reset(); g_time_seq = 1;
	assert(health_monitor_start(100) == -EAGAIN);
	assert(!g_sources[1].active && !g_channels[0].head);
	g_time_seq = 2;
	assert(!health_monitor_start(UINT32_MAX));
	assert(health_snapshot(1, &snapshot) && snapshot.timeout == 429496730ULL);
	assert(!health_monitor_stop()); tick(); assert(!g_count);
	set_time(UINT32_MAX - 1ULL);
	assert(!health_monitor_start(30));
	tick(); tick(); assert(g_now == 4294967296ULL);
	assert(health_clock(&now) && now == g_now);
	expect_fatal(HEALTH_TIMEOUT, 1);

	reset(); assert(!health_monitor_start(30));
	current.pid = 2; assert(!health_monitor_start(40));
	tick(); tick(); current.pid = 1;
	i = g_channels[0].head;
	assert(!health_monitor_kick()); assert(g_channels[0].head == i);
	assert(g_heap[0].deadline == 3);
	tick(); assert(g_heap[0].slot == 2); invariants();
	expect_fatal(HEALTH_TIMEOUT, 2);

	reset();
	for (i = 1; i < CONFIG_MAX_TASKS; i++) { current.pid = i; assert(!health_monitor_start(20)); }
	tick();
	for (i = 1; i < CONFIG_MAX_TASKS; i++) { current.pid = i; assert(!health_monitor_kick()); }
	tick(); invariants(); assert(g_heap[0].deadline == 3);
	expect_fatal(HEALTH_TIMEOUT, g_cache[g_heap[0].slot].pid);

	reset(); assert(!health_monitor_start(100));
	health_monitor_release(2); assert(!g_channels[0].fault);
	health_monitor_release(1); health_monitor_release(1);
	assert(g_channels[0].fault_pid == 1 && !g_fault);
	current.pid = 17;
	assert(health_monitor_start(100) == -ESHUTDOWN);
	expect_fatal(HEALTH_EXIT, 1);
	assert(!g_count && strstr(output, "HM source pid="));
	assert(strstr(output, "HM channel raw cpu="));

	reset(); assert(!health_monitor_start(10)); assert(!health_monitor_stop());
	health_monitor_release(1); tick(); assert(!g_fault && !g_count);
	current.pid = 17; assert(!health_monitor_start(100)); tick();
	assert(g_cache[1].pid == 17 && g_count == 1);
	assert(!health_monitor_stop()); tick();

	reset(); assert(!health_monitor_start(10)); set_time(1);
	assert(health_monitor_kick() == -ETIMEDOUT); expect_fatal(HEALTH_TIMEOUT, 1);
	reset(); assert(!health_monitor_start(10)); set_time(1);
	assert(health_monitor_stop() == -ETIMEDOUT); expect_fatal(HEALTH_TIMEOUT, 1);

	/* A finite full queue fails BEFORE any source mutation. */
	reset();
	for (i = 0; i < CONFIG_MAX_TASKS; i++) {
		assert(!(i & 1 ? health_monitor_stop() : health_monitor_start(10000)));
	}
	assert(health_monitor_start(10000) == -ENOSPC);
	assert(!g_sources[1].active);
	tick(); assert(!health_monitor_start(10000));
	reset();
	for (i = 0; i < CONFIG_MAX_TASKS - 1; i++) {
		assert(!(i & 1 ? health_monitor_stop() : health_monitor_start(10000)));
	}
	current.pid = 2; assert(!health_monitor_start(10000)); current.pid = 1;
	assert(health_monitor_stop() == -ENOSPC); assert(g_sources[1].active);
	assert(!health_monitor_kick()); /* Kick works even with membership full. */
	tick(); assert(!health_monitor_stop());

	/* No registration reads when root is in the future; kick is lazy. */
	reset(); assert(!health_monitor_start(1000)); tick(); source_reads = 0;
	tick(); assert(source_reads == 0);
	read_hook = kick_during_read;
	assert(!health_snapshot(1, &snapshot)); /* Reject a torn multiword read. */
	assert(health_snapshot(1, &snapshot));

	/* A busy first hint must not block another new task behind it. Even
	 * repeated read collisions with a progressing writer are not a stall.
	 */
	reset(); assert(!health_monitor_start(1000));
	read_hook = continually_renew;
	for (i = 0; i < 10; i++) tick();
	assert(!g_fault && g_channels[0].tail == g_channels[0].head);
	read_hook = NULL;
	current.pid = 2; assert(!health_monitor_start(10));
	read_hook = continually_renew; expect_fatal(HEALTH_TIMEOUT, 2);

	reset(); assert(!health_monitor_start(1000)); g_sources[1].seq = UINT32_MAX - 1;
	read_hook = kick_during_read;
	assert(!health_snapshot(1, &snapshot));
	assert(health_snapshot(1, &snapshot) && snapshot.epoch == 1 && snapshot.seq == 0);

	/* Interrupted readers see either coherent side of a time carry, or
	 * EAGAIN. They must never accept a mixed high/low time.
	 */
	for (i = 1; i <= 9; i++) {
		reset(); set_time(UINT32_MAX);
		accesses = 0; inject_at = i; inject_enabled = true;
		bool valid = health_clock(&now);
		inject_enabled = false;
		assert(!valid || now == UINT32_MAX || now == UINT32_MAX + 1ULL);
	}

	/* An unstable root does not hide another expired registration. */
	reset(); assert(!health_monitor_start(20));
	current.pid = 2; assert(!health_monitor_start(20)); tick();
	g_sources[1].seq++; expect_fatal(HEALTH_TIMEOUT, 2);
	reset(); assert(!health_monitor_start(20)); tick(); g_sources[1].seq++;
	tick(); tick(); expect_fatal(HEALTH_PUBLICATION, 1);

	reset(); g_sources[1].seq = UINT32_MAX - 1;
	g_channels[0].progress = UINT32_MAX - 1;
	assert(!health_monitor_start(1000));
	assert(g_sources[1].seq == 0 && g_sources[1].epoch == 1);
	assert(g_channels[0].progress == 0 && g_channels[0].progress_epoch == 1);
	tick(); assert(g_count == 1 && !g_fault);
	reset(); g_sources[1].seq = UINT32_MAX - 1; g_sources[1].epoch = UINT32_MAX;
	assert(health_monitor_start(10) == -EOVERFLOW);
	expect_fatal(HEALTH_EXHAUSTED, 1);
	reset(); g_channels[0].progress = UINT32_MAX - 1; g_channels[0].progress_epoch = UINT32_MAX;
	assert(health_monitor_start(10) == -EOVERFLOW);
	expect_fatal(HEALTH_EXHAUSTED, 1);

	reset(); assert(!health_monitor_start(1000)); tick();
	g_sources[1].seq++; set_time(UINT64_MAX - 1);
	expect_fatal(HEALTH_EXHAUSTED, 1); /* Retry key must never wrap to zero. */

#ifdef TEST_SMP
	/* CPU1 has no ownership of time, heap or fatal confirmation. */
	reset(); cpu = 1; health_monitor_tick(); assert(!g_now);
	assert(!health_monitor_start(10000));
	cpu = 0; assert(!health_monitor_stop()); assert(!health_monitor_start(200));
	/* CPU0 drains the later notifications before CPU1's old start hint. */
	tick(); assert(g_count == 1 && g_cache[1].deadline == 20);
	cpu = 1; assert(!health_monitor_kick());
	cpu = 0; assert(!health_monitor_stop());
	cpu = 1; health_monitor_release(1); current.pid = 17;
	assert(!health_monitor_start(300)); tick();
	assert(g_count == 1 && g_cache[1].pid == 17);

	/* Sample every access/barrier boundary of CPU1 start, kick and stop. */
	for (unsigned int op = 0; op < 3; op++) {
		for (i = 1; i < 100; i++) {
			reset(); cpu = 1;
			if (op) { assert(!health_monitor_start(10000)); tick(); }
			accesses = 0; inject_at = i; inject_enabled = true;
			int ret = op == 0 ? health_monitor_start(10000) :
				(op == 1 ? health_monitor_kick() : health_monitor_stop());
			inject_enabled = false;
			assert(ret == 0 || ret == -EAGAIN);
			tick(); invariants(); assert(!g_fault);
			assert(g_cache[1].active == (bool)g_sources[1].active);
		}
	}
	reset(); cpu = 1; store_hook = freeze_after_odd;
	if (!setjmp(panic_env)) { health_monitor_start(100); assert(!"stalled API must fault"); }
	assert(g_fault == HEALTH_PUBLICATION && panics == 1);

	reset(); cpu = 1; assert(!health_monitor_start(1000));
	health_monitor_release(1); cpu = 0; current.pid = 2;
	assert(!health_monitor_start(10));
	expect_fatal(HEALTH_EXIT, 1); /* Channel fault before heap timeout. */
#endif

	reset(); srand(7);
	for (i = 0; i < 100000; i++) {
		unsigned int slot = 1 + rand() % (CONFIG_MAX_TASKS - 1);
		current.pid = slot;
#ifdef TEST_SMP
		cpu = rand() & 1;
#endif
		int ret;
		if (!g_sources[slot].active) ret = health_monitor_start(100000 + rand() % 100000);
		else if (rand() & 1) ret = health_monitor_stop();
		else ret = health_monitor_kick();
		assert(ret == 0 || ret == -ENOSPC);
		if (!(i % 8) || ret == -ENOSPC) tick();
		invariants(); assert(!masked && !g_fault);
	}
	printf("PASS %s: API/queue errors, deadlines, lazy roots, exit, migration, word-boundary schedules, publication stalls, 100000 churn operations\n",
#ifdef TEST_SMP
		"SMP"
#else
		"UP"
#endif
	);
	return 0;
}
