/* Temporary real-kernel QEMU validation. Never enabled in a product build. */
#include <tinyara/config.h>
#include <tinyara/health_monitor.h>
#include <tinyara/irq.h>
#include <tinyara/sched.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include "sched/sched.h"
#include "health_monitor/health_monitor.h"
#include "health_monitor/tests/qemu/probe.h"

#define WORKERS 7
/* Controlled scenarios express intervals as ticks; natural tests keep ms. */
#define TICK_MS(n) ((unsigned long)((uint64_t)(n) * USEC_PER_TICK / 1000))
#define MAX_MS ((unsigned long)(((uint64_t)INT32_MAX * USEC_PER_TICK / 1000) > UINT32_MAX ? UINT32_MAX : ((uint64_t)INT32_MAX * USEC_PER_TICK / 1000)))
_Static_assert(USEC_PER_TICK >= 1000 && USEC_PER_TICK % 1000 == 0, "QEMU fixture requires whole-millisecond ticks");
#define OP_QUIT (-100)
#define OP_PTHREAD_EXIT (-101)
#define OP_PULSE (-102)
#define OP_STRESS (-103)
#define OP_BEGIN (-104)
static const char *g_case;
static unsigned int g_checks;
#define CHECK(expr) do { g_checks++; if (!(expr)) { \
	printf("HM_QEMU FAIL case=%s line=%d check=%s errno=%d\n", g_case, __LINE__, #expr, errno); \
	return -1; } } while (0)

static int delay_ms(unsigned int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
	while (nanosleep(&ts, &ts) < 0) { if (errno != EINTR) return -1; }
	return 0;
}

static int sem_take(sem_t *sem)
{
	int ret;
	do { ret = sem_wait(sem); } while (ret < 0 && errno == EINTR);
	return ret;
}

static struct health_monitor_s state_of(pid_t pid)
{
	struct health_monitor_s state = { UINT32_MAX, UINT32_MAX };
	irqstate_t flags = irqsave();
	struct tcb_s *tcb = sched_gettcb(pid);
	if (tcb) state = tcb->health_monitor;
	irqrestore(flags);
	return state;
}

struct actor {
	pthread_t thread;
	sem_t request;
	sem_t reply;
	int fd;
	int op;
	unsigned long arg;
	int result;
	int error;
	pid_t pid;
	uintptr_t tcb;
	uint32_t initial_timeout;
	unsigned int operations;
};

static void *actor_main(void *arg)
{
	struct actor *a = arg;
	unsigned int i;
	a->pid = getpid();
	a->tcb = (uintptr_t)this_task();
	a->initial_timeout = this_task()->health_monitor.timeout;
	sem_post(&a->reply);
	for (;;) {
		if (sem_take(&a->request) < 0) return (void *)1;
		if (a->op == OP_QUIT) return NULL;
		if (a->op == OP_PTHREAD_EXIT) pthread_exit((void *)0x1234);
		a->result = 0;
		a->error = 0;
		if (a->op == OP_PULSE) {
			for (i = 0; i < a->arg; i++) {
				if (delay_ms(20) < 0 || ioctl(a->fd, HMIOC_KICK, 0UL) < 0) { a->result = -1; break; }
				a->operations++;
			}
		} else if (a->op == OP_STRESS) {
			for (i = 0; i < a->arg; i++) {
				if (ioctl(a->fd, HMIOC_START, 5000UL) < 0 ||
								ioctl(a->fd, HMIOC_KICK, 0UL) < 0 ||
								ioctl(a->fd, HMIOC_STOP, 0UL) < 0) { a->result = -1; break; }
				a->operations++;
				if ((i & 63) == 0) delay_ms(1);
			}
			if (a->result == 0) a->result = ioctl(a->fd, HMIOC_START, 5000UL);
		} else {
			a->result = ioctl(a->fd, a->op, a->arg);
		}
		a->error = errno;
		sem_post(&a->reply);
	}
}

static int actor_open(struct actor *a, int fd)
{
	memset(a, 0, sizeof(*a));
	a->fd = fd;
	CHECK(sem_init(&a->request, 0, 0) == 0);
	CHECK(sem_init(&a->reply, 0, 0) == 0);
	CHECK(pthread_create(&a->thread, NULL, actor_main, a) == 0);
	CHECK(sem_take(&a->reply) == 0);
	CHECK(a->initial_timeout == 0);
	return 0;
}

static void actor_send(struct actor *a, int op, unsigned long arg)
{
	a->op = op;
	a->arg = arg;
	sem_post(&a->request);
}

static int actor_call(struct actor *a, int op, unsigned long arg)
{
	actor_send(a, op, arg);
	if (sem_take(&a->reply) < 0) return -2;
	return a->result;
}

static int actor_close(struct actor *a, int mode)
{
	void *result = NULL;
	if (mode == 1) CHECK(pthread_cancel(a->thread) == 0);
	else actor_send(a, mode == 2 ? OP_PTHREAD_EXIT : OP_QUIT, 0);
	CHECK(pthread_join(a->thread, &result) == 0);
	if (mode == 1) CHECK(result == PTHREAD_CANCELED);
	if (mode == 2) CHECK(result == (void *)0x1234);
	CHECK(sem_destroy(&a->request) == 0);
	CHECK(sem_destroy(&a->reply) == 0);
	return 0;
}

static int api(void)
{
	int fd, again;
	char value = 0x55;
	uint32_t at;
	struct health_monitor_s before, after;
	unsigned int i;
	const unsigned int ms[] = { 1, 9, 10, 11, 1000, INT32_MAX, (uint32_t)INT32_MAX + 1U, UINT32_MAX };
	hm_qemu_control(true, true, 100);
	CHECK(hm_qemu_count() == 0);
	fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	CHECK(fd >= 0);
	CHECK(hm_qemu_count() == 0);
	CHECK(health_monitor_next_check(&at) == 0);
	CHECK(ioctl(fd, HMIOC_KICK, 0UL) == 0);
	CHECK(ioctl(fd, HMIOC_STOP, 0UL) == -1 && errno == ENOENT);
	CHECK(ioctl(fd, HMIOC_START, 0UL) == -1 && errno == EINVAL);
	CHECK(ioctl(fd, HMIOC_START + 32, 0UL) == -1 && errno == ENOTTY);
	CHECK(ioctl(-1, HMIOC_KICK, 0UL) == -1 && errno == EBADF);
	CHECK(read(fd, &value, 1) == -1 && errno == ENOSYS && value == 0x55);
	CHECK(write(fd, &value, 1) == -1 && errno == ENOSYS);
	CHECK(read(fd, &value, 0) == -1 && errno == ENOSYS);
	CHECK(write(fd, &value, 0) == -1 && errno == ENOSYS);
	for (i = 0; i < sizeof(ms) / sizeof(ms[0]); i++) {
		uint64_t ticks = ((uint64_t)ms[i] * 1000 + USEC_PER_TICK - 1) / USEC_PER_TICK;
		if (ticks > INT32_MAX) {
			CHECK(ioctl(fd, HMIOC_START, (unsigned long)ms[i]) == -1 && errno == EINVAL);
			CHECK(state_of(getpid()).timeout == 0 && hm_qemu_count() == 0);
			continue;
		}
		CHECK(ioctl(fd, HMIOC_START, (unsigned long)ms[i]) == 0);
		before = state_of(getpid());
		CHECK(before.timeout == ticks && before.deadline == 100 + ticks);
		CHECK(ioctl(fd, HMIOC_START, 5000UL) == -1 && errno == EEXIST);
		after = state_of(getpid());
		CHECK(after.timeout == before.timeout && after.deadline == before.deadline);
		CHECK(ioctl(fd, HMIOC_STOP, 0UL) == 0);
		CHECK(state_of(getpid()).timeout == 0 && hm_qemu_count() == 0);
	}
	CHECK(ioctl(fd, HMIOC_START, TICK_MS(10)) == 0);
	CHECK(close(fd) == 0);
	CHECK(hm_qemu_count() == 1 && state_of(getpid()).timeout == 10);
	CHECK(ioctl(fd, HMIOC_KICK, 0UL) == -1 && errno == EBADF);
	again = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	CHECK(again >= 0);
	CHECK(ioctl(again, HMIOC_START, TICK_MS(10)) == -1 && errno == EEXIST);
	hm_qemu_control(true, true, 105);
	CHECK(ioctl(again, HMIOC_KICK, 0UL) == 0 && state_of(getpid()).deadline == 115);
	CHECK(ioctl(again, HMIOC_STOP, 0UL) == 0);
	CHECK(ioctl(again, HMIOC_STOP, 0UL) == -1 && errno == ENOENT);
	CHECK(close(again) == 0);
	CHECK(hm_qemu_count() == 0 && health_monitor_next_check(&at) == 0);
	hm_qemu_control(false, false, 0);
	return 0;
}

static int shared(void)
{
	struct actor a[2];
	int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	CHECK(fd >= 0);
	hm_qemu_control(true, true, 100);
	CHECK(actor_open(&a[0], fd) == 0 && actor_open(&a[1], fd) == 0);
	CHECK(actor_call(&a[0], HMIOC_START, TICK_MS(10)) == 0);
	CHECK(actor_call(&a[1], HMIOC_START, TICK_MS(20)) == 0);
	CHECK(hm_qemu_count() == 2);
	CHECK(state_of(a[0].pid).deadline == 110 && state_of(a[1].pid).deadline == 120);
	hm_qemu_control(true, true, 105);
	CHECK(ioctl(fd, HMIOC_KICK, 0UL) == 0);
	CHECK(state_of(a[0].pid).deadline == 110 && state_of(a[1].pid).deadline == 120);
	CHECK(actor_call(&a[0], HMIOC_KICK, 0) == 0);
	CHECK(state_of(a[0].pid).deadline == 115 && state_of(a[1].pid).deadline == 120);
	CHECK(actor_call(&a[0], HMIOC_STOP, 0) == 0 && hm_qemu_count() == 1);
	CHECK(state_of(a[1].pid).timeout == 20);
	CHECK(actor_close(&a[0], 0) == 0 && hm_qemu_count() == 1);
	CHECK(actor_close(&a[1], 2) == 0 && hm_qemu_count() == 0);
	CHECK(close(fd) == 0);
	hm_qemu_control(false, false, 0);
	return 0;
}

static sem_t g_task_ready;
static sem_t g_task_hold;
static volatile unsigned int g_task_runs;
static volatile bool g_task_return;
static volatile bool g_task_clean;
static volatile uintptr_t g_task_tcb;
static volatile int g_task_result;

static int task_entry(int argc, char *argv[])
{
	int fd;
	(void)argc; (void)argv;
	g_task_runs++;
	g_task_tcb = (uintptr_t)this_task();
	g_task_clean = this_task()->health_monitor.timeout == 0;
	fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	g_task_result = fd < 0 ? -1 : ioctl(fd, HMIOC_START, 500UL);
	if (fd >= 0) close(fd);
	sem_post(&g_task_ready);
	if (!g_task_return) sem_take(&g_task_hold);
	return g_task_result == 0 ? 0 : 1;
}

static int reap(pid_t pid)
{
	int status;
	pid_t ret = waitpid(pid, &status, 0);
	CHECK(ret == pid || (ret == -1 && errno == ECHILD));
	CHECK(sched_gettcb(pid) == NULL);
	return 0;
}

static int lifecycle(void)
{
	struct actor a;
	pid_t pid;
	uintptr_t first;
	int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	CHECK(fd >= 0);
	CHECK(actor_open(&a, fd) == 0);
	CHECK(actor_call(&a, HMIOC_START, 200) == 0);
	CHECK(actor_close(&a, 1) == 0 && hm_qemu_count() == 0);
	CHECK(delay_ms(250) == 0);
	CHECK(actor_open(&a, fd) == 0);
	CHECK(actor_call(&a, HMIOC_START, 200) == 0);
	CHECK(actor_close(&a, 2) == 0 && hm_qemu_count() == 0);
	CHECK(delay_ms(250) == 0);
	CHECK(sem_init(&g_task_ready, 0, 0) == 0 && sem_init(&g_task_hold, 0, 0) == 0);
	g_task_runs = 0; g_task_return = false;
	pid = task_create("hm_child", 100, 2048, task_entry, NULL);
	CHECK(pid > 0 && sem_take(&g_task_ready) == 0);
	CHECK(g_task_clean && g_task_result == 0 && hm_qemu_count() == 1);
	first = g_task_tcb;
	CHECK(task_restart(pid) == 0 && sem_take(&g_task_ready) == 0);
	CHECK(g_task_runs == 2 && g_task_clean && g_task_result == 0);
	CHECK(g_task_tcb == first && state_of(pid).timeout == 500000 / USEC_PER_TICK && hm_qemu_count() == 1);
	CHECK(task_delete(pid) == 0 && reap(pid) == 0 && hm_qemu_count() == 0);
	CHECK(delay_ms(550) == 0);
	g_task_return = true;
	pid = task_create("hm_child", 100, 2048, task_entry, NULL);
	CHECK(pid > 0 && sem_take(&g_task_ready) == 0 && reap(pid) == 0);
	CHECK(g_task_clean && g_task_result == 0 && hm_qemu_count() == 0);
	CHECK(delay_ms(550) == 0);
	CHECK(sem_destroy(&g_task_ready) == 0 && sem_destroy(&g_task_hold) == 0);
	CHECK(close(fd) == 0);
	printf("HM_QEMU lifecycle cancel=1 pthread_exit=1 task_restart=1 task_delete=1 task_return=1 same_tcb=1\n");
	return 0;
}

static int reuse(void)
{
	struct actor a;
	pid_t pid = -1, saved_last;
	uintptr_t first_tcb = 0;
	unsigned int i, pid_reused = 0, tcb_reused = 0;
	irqstate_t flags;
	int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	CHECK(fd >= 0);
	flags = irqsave(); saved_last = g_lastpid; irqrestore(flags);
	for (i = 0; i < 32; i++) {
		if (pid > 0) { flags = irqsave(); g_lastpid = pid - 1; irqrestore(flags); }
		CHECK(actor_open(&a, fd) == 0);
		if (i) {
			CHECK(a.pid == pid); pid_reused++;
			if (a.tcb == first_tcb) tcb_reused++;
			CHECK(delay_ms(100) == 0 && hm_qemu_count() == 0);
		} else { pid = a.pid; first_tcb = a.tcb; }
		CHECK(actor_call(&a, HMIOC_START, 60) == 0);
		CHECK(actor_close(&a, 0) == 0 && hm_qemu_count() == 0);
	}
	flags = irqsave(); if (g_lastpid < saved_last) g_lastpid = saved_last; irqrestore(flags);
	CHECK(pid_reused == 31 && tcb_reused > 0);
	CHECK(close(fd) == 0);
	printf("HM_QEMU reuse pid_reused=%u tcb_reused=%u pid=%d tcb=%lx cursor_controlled=1\n", pid_reused, tcb_reused, pid, (unsigned long)first_tcb);
	return 0;
}

static int active_or_stress(bool stress)
{
	struct actor a[WORKERS];
	unsigned int i, round, count = stress ? 10 : 1, ops = 0;
	int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	CHECK(fd >= 0);
	for (round = 0; round < count; round++) {
		for (i = 0; i < WORKERS; i++) CHECK(actor_open(&a[i], fd) == 0);
		if (!stress) {
			for (i = 0; i < WORKERS; i++) CHECK(actor_call(&a[i], HMIOC_START, 300 + i * 50) == 0);
			CHECK(hm_qemu_count() == WORKERS);
		}
		for (i = 0; i < WORKERS; i++) actor_send(&a[i], stress ? OP_STRESS : OP_PULSE, stress ? 1000 : 40);
		if (!stress) {
			char data[256], readback[256];
			int file = open("/tmp/hm-qemu-load", O_CREAT | O_TRUNC | O_RDWR, 0600);
			CHECK(file >= 0);
			for (i = 0; i < 100; i++) {
				memset(data, i, sizeof(data));
				CHECK(lseek(file, 0, SEEK_SET) == 0);
				CHECK(write(file, data, sizeof(data)) == sizeof(data));
				CHECK(lseek(file, 0, SEEK_SET) == 0);
				CHECK(read(file, readback, sizeof(readback)) == sizeof(readback));
				CHECK(memcmp(data, readback, sizeof(data)) == 0);
			}
			CHECK(close(file) == 0 && unlink("/tmp/hm-qemu-load") == 0);
		}
		for (i = 0; i < WORKERS; i++) {
			CHECK(sem_take(&a[i].reply) == 0 && a[i].result == 0);
			CHECK(a[i].operations == (stress ? 1000U : 40U));
			ops += a[i].operations;
		}
		CHECK(hm_qemu_count() == WORKERS);
		for (i = 0; i < WORKERS; i++) CHECK(actor_close(&a[i], 0) == 0);
		CHECK(hm_qemu_count() == 0);
	}
	CHECK(delay_ms(stress ? 5100 : 650) == 0);
	CHECK(close(fd) == 0);
	printf("HM_QEMU stress workers=%d rounds=%u operations=%u natural_time=1\n", WORKERS, count, ops);
	return 0;
}

static int many(void)
{
	unsigned int i, count = CONFIG_MAX_TASKS > 140 ? 128 : CONFIG_MAX_TASKS - 12;
	struct actor *a = calloc(count, sizeof(*a));
	int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	CHECK(a != NULL && fd >= 0 && count >= WORKERS);
	for (i = 0; i < count; i++) {
		CHECK(actor_open(&a[i], fd) == 0);
		CHECK(actor_call(&a[i], HMIOC_START, 5000 + i) == 0);
	}
	CHECK(hm_qemu_count() == count);
	for (i = 0; i < count; i++) actor_send(&a[i], OP_PULSE, 10);
	for (i = 0; i < count; i++) CHECK(sem_take(&a[i].reply) == 0 && a[i].result == 0);
	for (i = 0; i < count; i++) CHECK(actor_close(&a[i], 0) == 0);
	CHECK(hm_qemu_count() == 0);
	CHECK(delay_ms(5200) == 0 && close(fd) == 0);
	free(a);
	printf("HM_QEMU many real_workers=%u all_registered=1 exited_registered=1 survived_old_deadlines=1\n", count);
	return 0;
}

static int capacity(void)
{
	struct actor a[WORKERS];
	unsigned int i;
	int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	CHECK(fd >= 0);
	CHECK(HEALTH_MONITOR_HEAP_CAPACITY == 4);
	for (i = 0; i < 4; i++) {
		CHECK(actor_open(&a[i], fd) == 0);
		CHECK(actor_call(&a[i], HMIOC_START, 5000) == 0);
	}
	CHECK(hm_qemu_count() == 4);
	CHECK(ioctl(fd, HMIOC_START, 5000UL) == -1 && errno == ENOSPC);
	CHECK(state_of(getpid()).timeout == 0 && hm_qemu_count() == 4);
	CHECK(actor_call(&a[1], HMIOC_STOP, 0) == 0);
	CHECK(ioctl(fd, HMIOC_START, 5000UL) == 0 && hm_qemu_count() == 4);
	CHECK(ioctl(fd, HMIOC_STOP, 0UL) == 0);
	for (i = 0; i < 4; i++) CHECK(actor_close(&a[i], 0) == 0);
	CHECK(hm_qemu_count() == 0 && close(fd) == 0);
	printf("HM_QEMU capacity limit=4 rejected=ENOSPC recovered=1 real_threads=4\n");
	return 0;
}

static int settle(uint32_t now)
{
	hm_qemu_control(false, true, now);
	CHECK(delay_ms(35) == 0);
	hm_qemu_control(true, true, now);
	return 0;
}

static int boundary(void)
{
	int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	uint32_t at;
	CHECK(fd >= 0);
	hm_qemu_control(true, true, 100);
	CHECK(ioctl(fd, HMIOC_START, TICK_MS(2)) == 0);
	CHECK(settle(101) == 0 && hm_qemu_count() == 1);
	hm_qemu_control(true, true, 103);
	CHECK(ioctl(fd, HMIOC_KICK, 0UL) == 0 && state_of(getpid()).deadline == 105);
	CHECK(health_monitor_next_check(&at) == 1 && at == 102);
	CHECK(settle(104) == 0);
	CHECK(health_monitor_next_check(&at) == 1 && at == 105);
	hm_qemu_control(true, true, 110);
	CHECK(ioctl(fd, HMIOC_STOP, 0UL) == 0);
	CHECK(settle(110) == 0 && hm_qemu_count() == 0);
	hm_qemu_control(true, true, UINT32_MAX - 1);
	CHECK(ioctl(fd, HMIOC_START, TICK_MS(3)) == 0 && state_of(getpid()).deadline == 1);
	CHECK(health_monitor_next_check(&at) == 1 && at == 1);
	CHECK(settle(UINT32_MAX) == 0 && settle(0) == 0);
	CHECK(ioctl(fd, HMIOC_KICK, 0UL) == 0 && state_of(getpid()).deadline == 3);
	CHECK(settle(1) == 0);
	CHECK(health_monitor_next_check(&at) == 1 && at == 3);
	CHECK(ioctl(fd, HMIOC_STOP, 0UL) == 0 && settle(5) == 0);
	hm_qemu_control(true, true, 100);
	CHECK(ioctl(fd, HMIOC_START, TICK_MS(2)) == 0);
	hm_qemu_hint_busy(true);
	CHECK(settle(103) == 0);
	CHECK(health_monitor_next_check(&at) == -EAGAIN && hm_qemu_count() == 1);
	hm_qemu_hint_busy(false);
	CHECK(ioctl(fd, HMIOC_STOP, 0UL) == 0);
	CHECK(close(fd) == 0 && hm_qemu_count() == 0);
	hm_qemu_control(false, false, 0);
	printf("HM_QEMU boundary late_kick=1 stale_root_repair=1 wrap_survival=1 stop_due=1 unstable_hint_deferred=1 real_timer_irq=1\n");
	return 0;
}

static int fatal(const char *mode)
{
	struct actor a[3];
	unsigned int i;
	uint32_t now = 100, deadline = 102;
	pid_t target = getpid();
	int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	CHECK(fd >= 0);
	hm_qemu_control(true, true, now);
	if (!strcmp(mode, "multi")) {
		hm_qemu_control(false, false, 0);
		for (i = 0; i < 3; i++) CHECK(actor_open(&a[i], fd) == 0);
		CHECK(actor_call(&a[0], HMIOC_START, 500) == 0);
		CHECK(actor_call(&a[1], HMIOC_START, 200) == 0);
		CHECK(actor_call(&a[2], HMIOC_START, 600) == 0);
		actor_send(&a[0], OP_PULSE, 40);
		actor_send(&a[2], OP_PULSE, 40);
		target = a[1].pid;
		deadline = state_of(target).deadline;
		printf("HM_QEMU EXPECT case=%s pid=%d deadline=%lu natural=1\n", mode, target, (unsigned long)deadline);
	} else if (!strcmp(mode, "close")) {
		hm_qemu_control(false, false, 0);
		CHECK(ioctl(fd, HMIOC_START, 200UL) == 0);
		deadline = state_of(getpid()).deadline;
		CHECK(close(fd) == 0 && hm_qemu_count() == 1);
		printf("HM_QEMU EXPECT case=%s pid=%d deadline=%lu natural=1\n", mode, target, (unsigned long)deadline);
	} else {
		if (!strcmp(mode, "wrap")) {
			hm_qemu_control(true, true, UINT32_MAX - 1);
			CHECK(ioctl(fd, HMIOC_START, TICK_MS(2)) == 0);
			now = 0; deadline = 0;
		} else if (!strcmp(mode, "stale") || !strcmp(mode, "equal") || !strcmp(mode, "far")) {
			for (i = 0; i < 3; i++) CHECK(actor_open(&a[i], fd) == 0);
			CHECK(actor_call(&a[0], HMIOC_START, !strcmp(mode, "far") ? MAX_MS : TICK_MS(2)) == 0);
			CHECK(actor_call(&a[1], HMIOC_START, !strcmp(mode, "equal") ? TICK_MS(2) : TICK_MS(3)) == 0);
			CHECK(actor_call(&a[2], HMIOC_START, !strcmp(mode, "equal") ? TICK_MS(2) : TICK_MS(4)) == 0);
			if (!strcmp(mode, "stale")) {
				hm_qemu_control(true, true, 102);
				CHECK(actor_call(&a[0], HMIOC_KICK, 0) == 0);
				CHECK(actor_call(&a[2], HMIOC_KICK, 0) == 0);
			}
			target = !strcmp(mode, "equal") ? a[0].pid : a[1].pid;
			now = !strcmp(mode, "equal") ? 102 : 103;
			deadline = now;
		} else {
			CHECK(ioctl(fd, HMIOC_START, TICK_MS(2)) == 0);
			now = !strcmp(mode, "overdue") ? 103 : 102;
			if (!strcmp(mode, "unstable")) {
				uint32_t at;
				hm_qemu_hint_busy(true);
				CHECK(settle(103) == 0);
				CHECK(health_monitor_next_check(&at) == -EAGAIN);
				hm_qemu_hint_busy(false);
				now = 103;
			}
			if (!strcmp(mode, "late")) {
				hm_qemu_control(true, true, 103);
				CHECK(ioctl(fd, HMIOC_KICK, 0UL) == 0);
				CHECK(settle(104) == 0);
				now = 105; deadline = 105;
			}
		}
		CHECK(state_of(target).deadline == deadline);
		printf("HM_QEMU EXPECT case=%s pid=%d now=%lu deadline=%lu controlled=1\n", mode, target, (unsigned long)now, (unsigned long)deadline);
		fflush(stdout);
		hm_qemu_control(false, true, now);
	}
	fflush(stdout);
	for (;;) { CHECK(delay_ms(100) == 0); }
	return -1;
}

/* Reapplying console settings must leave TASH receive interrupts working. */
static int serial_settings(void)
{
#ifdef CONFIG_SERIAL_TERMIOS
	struct termios settings;
	unsigned i;
	int fd = open("/dev/console", O_RDWR);
	CHECK(fd >= 0);
	for (i = 0; i < 4; i++) {
		CHECK(tcgetattr(fd, &settings) == 0);
		CHECK(tcsetattr(fd, TCSANOW, &settings) == 0);
		CHECK(tcgetattr(fd, &settings) == 0);
	}
	CHECK(close(fd) == 0);
	return 0;
#else
	return -1;
#endif
}

int hm_qemu_main(int argc, char *argv[])
{
	int ret = -1;
	g_checks = 0;
	g_case = argc > 1 ? argv[1] : "missing";
	printf("HM_QEMU BEGIN case=%s capacity=%d\n", g_case, HEALTH_MONITOR_HEAP_CAPACITY);
	if (argc == 2) {
		if (!strcmp(g_case, "api")) ret = api();
		else if (!strcmp(g_case, "serial")) ret = serial_settings();
		else if (!strcmp(g_case, "shared")) ret = shared();
		else if (!strcmp(g_case, "lifecycle")) ret = lifecycle();
		else if (!strcmp(g_case, "reuse")) ret = reuse();
		else if (!strcmp(g_case, "active")) ret = active_or_stress(false);
		else if (!strcmp(g_case, "stress")) ret = active_or_stress(true);
		else if (!strcmp(g_case, "many")) ret = many();
		else if (!strcmp(g_case, "capacity")) ret = capacity();
		else if (!strcmp(g_case, "boundary")) ret = boundary();
	} else if (argc == 3 && !strcmp(g_case, "fatal")) ret = fatal(argv[2]);
	printf("HM_QEMU %s case=%s checks=%u count=%u\n", ret == 0 ? "PASS" : "FAIL", g_case, g_checks, hm_qemu_count());
	return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
