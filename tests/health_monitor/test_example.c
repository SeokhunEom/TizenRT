/* Exercise the actual example against the production core with fake ticks. */
#define main core_test_main
#include "test_core.c"
#undef main
#include <time.h>

static unsigned int sleep_calls;
static unsigned int forced_stop_errors;
static bool wrong_affinity;
#ifdef TEST_SMP
typedef uint32_t cpu_set_t;
#define CPU_ZERO(set) (*(set) = 0)
#define CPU_SET(index, set) (*(set) |= 1U << (index))
static cpu_set_t affinity = 3;
static int example_getaffinity(int pid, size_t size, cpu_set_t *set)
{
	(void)pid; assert(size == sizeof(*set)); *set = affinity; return 0;
}
static int example_setaffinity(int pid, size_t size, const cpu_set_t *set)
{
	(void)pid; assert(size == sizeof(*set)); affinity = *set;
	if (!wrong_affinity) cpu = (*set & 1) ? 0 : 1;
	return 0;
}
#define sched_getaffinity example_getaffinity
#define sched_setaffinity example_setaffinity
#endif
static int example_nanosleep(const struct timespec *delay, struct timespec *remaining)
{
	(void)remaining;
	sleep_calls++;
	uint64_t usec = (uint64_t)delay->tv_sec * 1000000 + delay->tv_nsec / 1000;
	for (uint64_t i = 0; i < (usec + USEC_PER_TICK - 1) / USEC_PER_TICK; i++) tick();
	return 0;
}
static int example_stop(void)
{
	if (forced_stop_errors) { forced_stop_errors--; return -ENOSPC; }
	return health_monitor_stop();
}
#define nanosleep example_nanosleep
#define health_monitor_stop example_stop
#include "../../apps/examples/health_monitor/health_monitor_main.c"
#undef health_monitor_stop

static void fresh(void)
{
	reset(); sleep_calls = forced_stop_errors = 0; wrong_affinity = false;
#ifdef TEST_SMP
	affinity = 3;
#endif
}
int main(void)
{
	char *healthy[] = {"health_monitor", "healthy", "3", "2000"};
	char *restart[] = {"health_monitor", "restart", "3", "2000"};
	char *errors[] = {"health_monitor", "errors"};
	char *bad[] = {"health_monitor", "healthy", "-1"};
	char *bad_timeout[] = {"health_monitor", "timeout", "0"};
	char *help[] = {"health_monitor"};
	char *timeout[] = {"health_monitor", "timeout", "100"};
	char *exit_cmd[] = {"health_monitor", "exit", "2000"};
	char *migrate[] = {"health_monitor", "migrate", "4", "2000"};
	fresh(); assert(!health_monitor_main(1, help)); assert(!g_sources[1].active);
	assert(health_monitor_main(3, bad) == EXIT_FAILURE);
	assert(health_monitor_main(3, bad_timeout) == EXIT_FAILURE);
	assert(!g_sources[1].active && !g_channels[0].head);
	fresh(); assert(!health_monitor_main(4, healthy)); tick();
	assert(!g_sources[1].active && !g_count && !g_fault && sleep_calls == 3);
	fresh(); assert(!health_monitor_main(4, restart)); tick();
	assert(!g_sources[1].active && !g_count && !g_fault && sleep_calls == 6);
	fresh(); assert(!health_monitor_main(2, errors)); tick(); assert(!g_count);
	fresh(); g_channels[0].head = CONFIG_MAX_TASKS; /* full queue of empty-slot hints */
	assert(!health_monitor_main(4, healthy)); assert(sleep_calls == 4);
	fresh(); forced_stop_errors = HM_RETRIES;
	assert(health_monitor_main(4, healthy) == EXIT_FAILURE);
	assert(g_sources[1].active); /* A failed cleanup must never claim PASS. */
	fresh();
#ifdef TEST_SMP
	assert(!health_monitor_main(4, migrate)); tick();
	assert(!g_sources[1].active && !g_count && affinity == 3);
	fresh(); wrong_affinity = true;
	assert(health_monitor_main(4, migrate) == EXIT_FAILURE);
	assert(!g_sources[1].active && affinity == 3);
#else
	assert(health_monitor_main(4, migrate) == EXIT_FAILURE);
	assert(!g_sources[1].active);
#endif
	fresh();
	if (!setjmp(panic_env)) { health_monitor_main(3, timeout); assert(!"timeout should panic"); }
	assert(g_fault == HEALTH_TIMEOUT && g_fault_pid == 1);
	assert(strstr(output, "HM fault="));
	fresh(); assert(!health_monitor_main(3, exit_cmd)); assert(g_sources[1].active);
	health_monitor_release(1); expect_fatal(HEALTH_EXIT, 1);
	puts("PASS example: healthy/restart/errors, bounded queue retry, failed-stop cleanup, migration, timeout/exit fatal paths");
	return 0;
}
