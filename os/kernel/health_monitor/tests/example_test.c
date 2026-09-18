/* SPDX-License-Identifier: Apache-2.0 */
/* Run the real example through the existing real registry/driver/VFS fixture.
 * Only pthread affinity/lifecycle, time and device storage are host models.
 * Physical timeout, SVC, PM and reset behavior are not exercised here.
 */
#define _GNU_SOURCE
#include <semaphore.h>
#include <time.h>
#define main driver_fixture_main
#include "driver_test.c"
#undef main
#undef open
#undef close

struct launch_s {
	void *(*entry)(void *arg);
	void *arg;
	struct tcb_s tcb;
};

static unsigned int g_app_creates;
static unsigned int g_app_create_failure;
static unsigned int g_app_starts;
static unsigned int g_app_kicks;
static unsigned int g_app_stops;
static unsigned int g_app_delays;
static unsigned int g_app_interrupts;
#ifdef CONFIG_SMP
static unsigned int g_app_affinities;
#endif
static int g_app_fail_command;
static int g_app_reason = 62;

#include "../../../include/sys/prctl.h"
static int app_prctl(int option, ...)
{
	assert(option == PR_REBOOT_REASON_READ);
	return g_app_reason;
}

static void *app_entry(void *arg)
{
	struct launch_s *launch = arg;
	void *result;

	g_current = &launch->tcb;
	health_monitor_task_init(g_current);
	result = launch->entry(launch->arg);
	/* Model task exit at the same ownership boundary as the kernel hook. */
	health_monitor_cleanup(g_current);
	assert(!g_current->health_monitor.timeout);
	free(launch);
	return result;
}

static int app_create(pthread_t *thread, const pthread_attr_t *attr,
	void *(*entry)(void *arg), void *arg)
{
	struct launch_s *launch;
	int ret;

	(void)attr;
	if (++g_app_creates == g_app_create_failure) {
		return EAGAIN;
	}
	launch = calloc(1, sizeof(*launch));
	assert(launch);
	launch->entry = entry;
	launch->arg = arg;
	launch->tcb.pid = g_app_creates + 10;
	ret = pthread_create(thread, NULL, app_entry, launch);
	if (ret != 0) {
		free(launch);
	}
	return ret;
}

#ifdef CONFIG_SMP
static int app_affinity(pthread_attr_t *attr, size_t size, const cpu_set_t *set)
{
	(void)attr;
	assert(size == sizeof(*set));
	assert(CPU_COUNT(set) == 1);
	assert(CPU_ISSET(g_app_affinities % 2, set));
	g_app_affinities++;
	return 0; /* Host CPU numbering/affinity is not the target's two CPUs. */
}
#endif

static int app_delay(const struct timespec *request, struct timespec *remaining)
{
	uint32_t delta = request->tv_sec * 1000 + request->tv_nsec / 1000000;
	g_app_delays++;
	if (g_app_interrupts) {
		g_app_interrupts--;
		*remaining = *request;
		errno = EINTR;
		return -1;
	}
	g_now += delta;
	if (g_health_count) {
		assert(g_health_count == 1 && g_current->health_monitor.timeout);
		assert((int32_t)(g_current->health_monitor.deadline - g_now) > 0);
	}
	return 0;
}

static int app_ioctl(int fd, int cmd, unsigned long arg)
{
	if (cmd == g_app_fail_command) {
		errno = EIO;
		return -1;
	}
	if (cmd == HMIOC_START) {
		__atomic_fetch_add(&g_app_starts, 1, __ATOMIC_RELAXED);
	} else if (cmd == HMIOC_KICK) {
		__atomic_fetch_add(&g_app_kicks, 1, __ATOMIC_RELAXED);
	} else if (cmd == HMIOC_STOP) {
		__atomic_fetch_add(&g_app_stops, 1, __ATOMIC_RELAXED);
	}
	return fs_ioctl(fd, cmd, arg);
}

#define open hm_test_open
#define close hm_test_close
#define ioctl app_ioctl
#define pthread_create app_create
#define pthread_attr_setaffinity_np app_affinity
#define nanosleep app_delay
#define CONFIG_SYSTEM_REBOOT_REASON 1
#define prctl app_prctl
#include "../../../../apps/examples/health_monitor/health_monitor_main.c"
#undef prctl
#undef nanosleep
#undef pthread_attr_setaffinity_np
#undef pthread_create
#undef ioctl
#undef close
#undef open

static int command(int argc, char **argv)
{
	int ret = health_monitor_main(argc, argv);
	assert(g_health_count == 0 && g_inode.refs == 0);
	assert(!g_irq_masked && !g_lock_held);
	return ret;
}

int main(void)
{
	char *run[] = {"health_monitor", "run", "1000", "100", "3"};
	char *stop[] = {"health_monitor", "stop", "1000"};
	char *leave[] = {"health_monitor", "exit", "1000", "3"};
	char *smp[] = {"health_monitor", "smp", "1000", "3"};
	char *bad[] = {"health_monitor", "run", "1000", "1000", "1"};
	char *zero_period[] = {"health_monitor", "run", "1000", "0", "1"};
	char *bad_number[] = {"health_monitor", "stop", "-1"};
	char *overflow[] = {"health_monitor", "stop", "4294967297"};
	char *reason[] = {"health_monitor", "reason"};
	unsigned int number;
	unsigned int before;

	driver_fixture_main();
	g_current = &g_tasks[0];
	g_now = 0;
	assert(command(2, reason) == EXIT_SUCCESS);
	g_app_reason = -1;
	assert(command(2, reason) == EXIT_FAILURE);
	g_app_interrupts = 1;
	assert(command(5, run) == EXIT_SUCCESS);
	assert(g_app_starts == 1 && g_app_kicks == 3 && g_app_stops == 1);
	assert(g_app_delays == 4 && g_now == 300);
	assert(command(3, stop) == EXIT_SUCCESS);
	assert(g_now == 1400);
	assert(command(4, leave) == EXIT_SUCCESS);
	assert(g_app_creates == 3);
#ifdef CONFIG_SMP
	before = g_app_starts;
	assert(command(4, smp) == EXIT_SUCCESS);
	assert(g_app_starts - before == 6 * 33);
	assert(g_app_affinities == 6);
	g_app_create_failure = g_app_creates + 2;
	assert(command(4, smp) == EXIT_FAILURE); /* Join the first worker on failure. */
#else
	assert(command(4, smp) == EXIT_FAILURE);
#endif
	before = g_app_starts;
	assert(command(5, bad) == EXIT_FAILURE);
	assert(command(5, zero_period) == EXIT_FAILURE);
	assert(command(3, bad_number) == EXIT_FAILURE);
	assert(command(3, overflow) == EXIT_FAILURE);
	assert(command(2, stop) == EXIT_FAILURE);
	assert(g_app_starts == before);
	assert(hm_number("", 100, &number) < 0);
	assert(hm_number("1x", 100, &number) < 0);
	assert(hm_number("+1", 100, &number) < 0);
	assert(hm_number("60000", 60000, &number) == 0 && number == 60000);
	g_app_fail_command = HMIOC_KICK;
	assert(command(5, run) == EXIT_FAILURE); /* Failed kick must still STOP. */
	g_app_fail_command = HMIOC_START;
	assert(command(3, stop) == EXIT_FAILURE);
	g_app_fail_command = 0;
	puts("PASS: example commands through real ioctl/VFS/registry; host lifecycle/time models");
	return 0;
}
