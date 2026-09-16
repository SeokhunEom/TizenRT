/* SPDX-License-Identifier: Apache-2.0 */
/* Real registry, inspector and PM entry; simulated scheduler/board/time. */
#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <tinyara/config.h>
#include <tinyara/pm/pm.h>

#define CONFIG_PM 1
#ifndef HEALTH_MONITOR_TEST_NO_TICKSUPPRESS
#define CONFIG_PM_TICKSUPPRESS 1
#ifndef HEALTH_MONITOR_TEST_NO_TIMEDWAKEUP
#define CONFIG_PM_TIMEDWAKEUP 1
#define CONFIG_PM_SLEEP_ENTRY_WAIT_MS 10
#endif
#endif
#ifdef HEALTH_MONITOR_TEST_OFF
#undef CONFIG_HEALTH_MONITOR
#endif

#ifdef CONFIG_HEALTH_MONITOR
static void __test_panic(void);
#undef PANIC
#define PANIC() __test_panic()
#include "../health_monitor.c"
#endif
/* Older glibc declares stime(); isolate the file-local target variable. */
#define stime test_pm_stime
#include "../../../pm/pm_idle.c"
#undef stime

struct pm_global_s g_pmglobals;
static struct pm_sleep_ops g_ops;
static struct tcb_s g_task;
static struct tcb_s g_idle[CONFIG_SMP_NCPUS];
static clock_t g_now;
static clock_t g_wdog_delay;
static clock_t g_missing_ticks;
static clock_t g_compensated_ticks;
static clock_t g_prepare_ticks;
static clock_t g_resume_ticks;
static int g_suspend_result;
static int g_timer_result;
static int g_sleep_result;
static unsigned int g_irq_masked;
static unsigned int g_global_lock;
static unsigned int g_sched_lock;
static unsigned int g_registry_locks;
static unsigned int g_sleep_calls;
static unsigned int g_timer_calls;
static unsigned int g_timer_us;
static unsigned int g_disable_calls;
static unsigned int g_enable_calls;
static unsigned int g_suspend_calls;
static unsigned int g_resume_calls;
static unsigned int g_cpuoff_calls;
static unsigned int g_cpuon_calls;
static unsigned int g_missing_reads;
static void (*g_on_suspend)(void);
static void (*g_on_cpuoff)(void);
static void (*g_on_sleep)(void);
#ifdef CONFIG_HEALTH_MONITOR
static jmp_buf g_panic_return;
static bool g_expect_panic;
#endif

irqstate_t irqsave(void)
{
	unsigned int previous = g_irq_masked;
	g_irq_masked = 1;
	return previous;
}

void irqrestore(irqstate_t flags)
{
	g_irq_masked = flags;
}

void spin_lock_wo_note(volatile spinlock_t *lock)
{
	assert(g_irq_masked);
	g_registry_locks++;
	assert(__atomic_exchange_n(lock, SP_LOCKED, __ATOMIC_ACQUIRE) == SP_UNLOCKED);
}

void spin_unlock_wo_note(volatile spinlock_t *lock)
{
	assert(g_irq_masked);
	__atomic_store_n(lock, SP_UNLOCKED, __ATOMIC_RELEASE);
}

struct tcb_s *this_task(void)
{
	return &g_task;
}

clock_t clock_systimer(void)
{
	return g_now;
}

irqstate_t enter_critical_section(void)
{
	assert(!g_global_lock);
	g_global_lock = 1;
	return irqsave();
}

void leave_critical_section(irqstate_t flags)
{
	assert(g_global_lock == 1 && !g_sched_lock);
	g_global_lock = 0;
	irqrestore(flags);
}

int sched_lock(void)
{
	assert(g_global_lock && !g_sched_lock);
	g_sched_lock = 1;
	return OK;
}

int sched_unlock(void)
{
	assert(g_global_lock && g_sched_lock);
	g_sched_lock = 0;
	return OK;
}

cpu_set_t sched_getactivecpu(void)
{
	return (1u << CONFIG_SMP_NCPUS) - 1;
}

struct tcb_s *current_task(int cpu)
{
	return &g_idle[cpu];
}

int sched_cpuoff(int cpu)
{
	assert(cpu > 0 && g_global_lock && g_irq_masked);
	g_cpuoff_calls++;
	if (g_on_cpuoff) {
		g_on_cpuoff();
	}
	return OK;
}

int sched_cpuon(int cpu)
{
	assert(cpu > 0 && g_global_lock && g_irq_masked);
	g_cpuon_calls++;
	return OK;
}

enum pm_state_e pm_checkstate(void)
{
	return PM_SLEEP;
}

int pm_changestate(enum pm_state_e newstate)
{
	assert(g_global_lock && g_sched_lock && g_irq_masked);
	if (newstate == PM_SLEEP) {
		g_suspend_calls++;
		if (g_on_suspend) {
			g_on_suspend();
		}
	} else {
		g_resume_calls++;
		g_prepare_ticks += g_resume_ticks;
	}
	return newstate == PM_SLEEP ? g_suspend_result : OK;
}

int up_timer_disable(void)
{
	g_disable_calls++;
	return OK;
}

int up_timer_enable(void)
{
	g_enable_calls++;
	return OK;
}

clock_t wd_getwakeupdelay(void)
{
	assert(g_global_lock && g_irq_masked);
	return g_wdog_delay;
}

void clock_timer_nohz(clock_t ticks)
{
	assert(g_global_lock && g_irq_masked);
	g_now += ticks;
	g_compensated_ticks += ticks;
}

void wd_timer_nohz(clock_t ticks)
{
	assert(g_compensated_ticks == ticks);
}

static int __board_set_timer(unsigned int delay_us)
{
	g_timer_calls++;
	g_timer_us = delay_us;
	return g_timer_result;
}

static int __board_sleep(void)
{
	assert(g_global_lock && g_sched_lock && g_irq_masked);
	assert(g_disable_calls == 1 && !g_enable_calls);
	g_sleep_calls++;
	if (g_on_sleep) {
		g_on_sleep();
	}
	return g_sleep_result;
}

static clock_t __board_elapsed_ticks(void)
{
	assert(g_disable_calls && !g_enable_calls);
	return g_prepare_ticks + (g_sleep_calls ? g_missing_ticks : 0);
}

static clock_t __board_missing_ticks(void)
{
	g_missing_reads++;
	return g_missing_ticks;
}

static void __reset_test(clock_t now)
{
	assert(!g_global_lock && !g_sched_lock && !g_irq_masked);
	memset(&g_task, 0, sizeof(g_task));
	g_task.pid = 10;
	for (int cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++) {
		g_idle[cpu].pid = cpu;
	}
#ifdef CONFIG_HEALTH_MONITOR
	g_health_count = 0;
	memset(g_health_heap, 0, sizeof(g_health_heap));
	g_health_sequence = 0;
	health_monitor_publish();
	g_expect_panic = false;
#endif
	g_now = now;
	test_pm_stime = 0;
	g_wdog_delay = g_missing_ticks = g_compensated_ticks = 0;
	g_prepare_ticks = g_resume_ticks = 0;
	g_suspend_result = g_timer_result = g_sleep_result = OK;
	g_registry_locks = g_sleep_calls = g_timer_calls = g_timer_us = 0;
	g_disable_calls = g_enable_calls = g_suspend_calls = g_resume_calls = 0;
	g_cpuoff_calls = g_cpuon_calls = g_missing_reads = 0;
	g_on_suspend = g_on_cpuoff = g_on_sleep = NULL;
	g_ops = (struct pm_sleep_ops) {
		.sleep = __board_sleep,
		.set_timer = __board_set_timer,
		.get_missingtick = __board_missing_ticks,
		.get_elapsedtick = __board_elapsed_ticks,
	};
	g_pmglobals.is_running = true;
	g_pmglobals.sleep_ops = &g_ops;
}

static void __run_pm(bool sleep, unsigned int timer_us)
{
	pm_idle();
	assert(!g_global_lock && !g_sched_lock && !g_irq_masked);
	assert(g_sleep_calls == (unsigned int)sleep);
	assert(g_timer_calls == (unsigned int)(timer_us > 0));
	assert(g_timer_us == timer_us);
	assert(g_cpuoff_calls == g_cpuon_calls);
	assert(g_suspend_calls == g_resume_calls);
	assert(g_disable_calls == g_enable_calls);
}

#ifdef CONFIG_PM_TIMEDWAKEUP
static void __prepare_25_ticks(void)
{
	assert(g_irq_masked && g_disable_calls == 1);
	g_prepare_ticks += 25;
}

static void __sleep_to_timer(void)
{
	g_missing_ticks = g_timer_us / 1000;
}

#endif

static void __test_existing_wakeup(void)
{
	__reset_test(100);
	__run_pm(true, 0);
#ifdef CONFIG_PM_TIMEDWAKEUP
	__reset_test(100);
	g_wdog_delay = 20;
	__run_pm(true, 20000);
	__reset_test(100);
	g_wdog_delay = 9;
	__run_pm(false, 0);
	__reset_test(100);
	g_wdog_delay = (clock_t)INT_MAX + 100u;
	__run_pm(true, UINT_MAX);
	__reset_test(100);
	g_wdog_delay = 100;
	g_on_suspend = __prepare_25_ticks;
	g_on_sleep = __sleep_to_timer;
	__run_pm(true, 75000); /* Also runs with HM and IRQ WDT disabled. */
	assert(g_now == 200 && g_compensated_ticks == 100);
	__reset_test(100);
	g_ops.get_elapsedtick = NULL; /* Legacy board, no active monitor. */
	g_missing_ticks = 20;
	__run_pm(true, 0);
	assert(g_now == 120 && g_missing_reads == 1);
#endif
}

#ifdef CONFIG_HEALTH_MONITOR
static void __test_panic(void)
{
	assert(g_expect_panic && !g_global_lock);
#ifdef CONFIG_SMP
	assert(g_health_lock == SP_UNLOCKED);
#endif
	longjmp(g_panic_return, 1);
}

static void __inspect(bool panic)
{
	g_expect_panic = panic;
	g_irq_masked = 1;
	if (setjmp(g_panic_return) == 0) {
		health_monitor_timer();
		assert(!panic);
	} else {
		assert(panic);
	}
	g_irq_masked = 0;
	g_expect_panic = false;
}

static void __test_publication_and_lock(void)
{
	__reset_test(100);
	assert(health_monitor_start(20) == OK);
	g_health_sequence |= 1u;
	__run_pm(false, 0); /* Publication overlap must not become an empty hint. */

	__reset_test(100);
	assert(health_monitor_start(20) == OK);
	unsigned int locks = g_registry_locks;
#ifdef CONFIG_SMP
	g_health_lock = SP_LOCKED;
#endif
	g_irq_masked = g_global_lock = 1;
#ifdef CONFIG_PM_TIMEDWAKEUP
	assert(get_next_wakeup_time(0) == 20);
#else
	assert(get_next_wakeup_time(0) == ERROR);
#endif
	assert(g_registry_locks == locks);
	g_irq_masked = g_global_lock = 0;
#ifdef CONFIG_SMP
	g_health_lock = SP_UNLOCKED;
#endif
}

#ifdef CONFIG_PM_TIMEDWAKEUP
static void __test_selection_and_boundaries(void)
{
	for (unsigned int wdog = 0; wdog <= 40; wdog += 10) {
		__reset_test(100);
		assert(health_monitor_start(20) == OK);
		g_wdog_delay = wdog;
		unsigned int locks = g_registry_locks;
		__run_pm(true, (wdog > 0 && wdog < 20 ? wdog : 20) * 1000);
		assert(g_registry_locks == locks);
		assert(g_task.health_monitor.deadline == 120);
		assert(g_health_heap[0].check_at == 120);
	}
	for (unsigned int offset = 0; offset <= 21; offset++) {
		__reset_test(100);
		assert(health_monitor_start(20) == OK);
		g_now += offset;
		bool sleep = offset <= 10;
		__run_pm(sleep, sleep ? (20 - offset) * 1000 : 0);
	}

	__reset_test((clock_t)UINT32_MAX - 19);
	assert(health_monitor_start(20) == OK);
	assert(g_task.health_monitor.deadline == 0);
	__run_pm(true, 20000);
	g_now += 20;
	g_sleep_calls = g_timer_calls = g_timer_us = 0;
	__run_pm(false, 0); /* Absolute tick zero is due after wrap, not absent. */

	__reset_test(100);
	assert(health_monitor_start(INT32_MAX) == OK);
	__run_pm(true, UINT_MAX);
	assert(g_task.health_monitor.deadline == 100u + INT32_MAX);
}

static void __start_short(void)
{
	assert(health_monitor_start(1) == OK);
}

static void __start_earlier(void)
{
	assert(health_monitor_start(20) == OK);
}

static void __stop_monitor(void)
{
	assert(health_monitor_stop() == OK);
}

static void __publishing(void)
{
	g_health_sequence |= 1u;
}

static void __test_final_snapshot(void)
{
	__reset_test(100);
	g_on_suspend = __start_short;
	g_missing_ticks = 99; /* A deferral must not consume an old sleep sample. */
	__run_pm(false, 0);
	assert(g_suspend_calls == 1 && g_disable_calls == 1 && !g_missing_reads);
	assert(g_now == 100);

	__reset_test(100);
	g_wdog_delay = 50;
	g_on_suspend = __start_earlier;
	__run_pm(true, 20000);

	__reset_test(100);
	assert(health_monitor_start(20) == OK);
	g_on_suspend = __stop_monitor;
	__run_pm(true, 0);

	__reset_test(100);
	g_on_suspend = __publishing;
	__run_pm(false, 0);
	assert(g_suspend_calls == 1 && g_disable_calls == 1 && !g_missing_reads);
#ifdef CONFIG_SMP
	__reset_test(100);
	g_on_cpuoff = __start_earlier;
	__run_pm(true, 20000);
	assert(g_cpuoff_calls == CONFIG_SMP_NCPUS - 1);
#endif
}

static void __test_sleep_time_and_kick(void)
{
	for (unsigned int kick = 0; kick < 2; kick++) {
		__reset_test(100);
		assert(health_monitor_start(20) == OK);
		g_missing_ticks = 25; /* Wake late; the existing late-KICK policy applies. */
		__run_pm(true, 20000);
		assert(g_now == 125 && g_compensated_ticks == 25);
		assert(g_task.health_monitor.deadline == 120);
		if (kick) {
			health_monitor_kick();
		}
		__inspect(!kick);
		if (kick) {
			assert(g_health_heap[0].check_at == 145);
		}
	}

	__reset_test(100);
	assert(health_monitor_start(30) == OK);
	g_now = 110;
	health_monitor_kick();
	g_missing_ticks = 20;
	__run_pm(true, 20000); /* Stale check_at 130 wakes before latest deadline 140. */
	assert(g_now == 130 && g_task.health_monitor.deadline == 140);
	assert(g_health_heap[0].check_at == 130);
	__inspect(false);
	assert(g_health_heap[0].check_at == 140);
	g_now = 140;
	__inspect(true);
}

static void __prepare_125_ticks(void)
{
	assert(g_irq_masked && g_disable_calls == 1);
	g_prepare_ticks += 125;
}

static void __test_transition_time(void)
{
	__reset_test(100);
	assert(health_monitor_start(100) == OK);
	g_on_suspend = __prepare_125_ticks;
	g_missing_ticks = 999; /* Previous sleep sample must not be consumed. */
	__run_pm(false, 0);
	assert(g_now == 225 && g_compensated_ticks == 125 && !g_missing_reads);
	__inspect(true);

	__reset_test(100);
	assert(health_monitor_start(100) == OK);
	g_on_suspend = __prepare_25_ticks;
	g_on_sleep = __sleep_to_timer;
	g_resume_ticks = 3;
	__run_pm(true, 75000);
	assert(g_now == 203 && g_compensated_ticks == 103 && !g_missing_reads);
	__inspect(true);

	__reset_test(100);
	g_wdog_delay = 100;
	g_on_suspend = __prepare_25_ticks;
	g_on_sleep = __sleep_to_timer;
	__run_pm(true, 75000); /* Software timers use the same frozen OS clock. */
	assert(g_now == 200 && g_compensated_ticks == 100);

	for (unsigned int failure = 0; failure < 3; failure++) {
		__reset_test(100);
		assert(health_monitor_start(100) == OK);
		g_on_suspend = __prepare_25_ticks;
		g_resume_ticks = 3;
		g_missing_ticks = 999;
		if (failure == 0) {
			g_suspend_result = ERROR;
		} else if (failure == 1) {
			g_timer_result = ERROR;
		} else {
			g_sleep_result = ERROR;
			g_missing_ticks = 5;
		}
		__run_pm(failure == 2, failure ? 75000 : 0);
		assert(g_now == (failure == 2 ? 133 : 128));
		assert(!g_missing_reads);
	}
}

static void __test_missing_operations(void)
{
	for (unsigned int missing = 0; missing < 3; missing++) {
		__reset_test(100);
		assert(health_monitor_start(20) == OK);
		if (missing == 0) {
			g_ops.set_timer = NULL;
		} else if (missing == 1) {
			g_ops.get_elapsedtick = NULL;
		} else {
			g_ops.sleep = NULL;
		}
		__run_pm(false, 0);
		assert(!g_suspend_calls);
	}
}
#else
static void __test_unsupported_sleep(void)
{
	__reset_test(100);
	assert(health_monitor_start(20) == OK);
	__run_pm(false, 0);
	assert(!g_suspend_calls);
	assert(health_monitor_stop() == OK);
	g_now++;
	__run_pm(true, 0);
	__inspect(false);
}
#endif
#endif

int main(void)
{
	__test_existing_wakeup();
#ifdef CONFIG_HEALTH_MONITOR
	__test_publication_and_lock();
#ifdef CONFIG_PM_TIMEDWAKEUP
	__test_selection_and_boundaries();
	__test_final_snapshot();
	__test_sleep_time_and_kick();
	__test_missing_operations();
	__test_transition_time();
#else
	__test_unsupported_sleep();
#endif
#endif
	puts("PASS: PM wakeup selection, sleep accounting, final snapshot and monitor policy");
	return 0;
}
