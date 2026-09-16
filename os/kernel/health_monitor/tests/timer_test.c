/* SPDX-License-Identifier: Apache-2.0 */
/* Real registry, tick entry and assert-reason policy; host OS primitives. */
#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <tinyara/config.h>
#include <tinyara/spinlock.h>

#ifndef HEALTH_MONITOR_TEST_NO_REASON
#define CONFIG_SYSTEM_REBOOT_REASON 1
#endif
#define CONFIG_DEBUG_ERROR 1
#define HEALTH_MONITOR_TEST_TIMEOUT_LOG 1
#define CONFIG_RR_INTERVAL 0
#define CONFIG_SCHED_CPULOAD 1

static unsigned int g_cpu;
static void test_panic(void);
void __attribute__((weak)) sched_process_cpuload(void);
#ifdef CONFIG_SMP
static irqstate_t enter_critical_section(void);
static void leave_critical_section(irqstate_t flags);
static bool test_compare_exchange(volatile spinlock_t *lock, spinlock_t *expected,
	spinlock_t desired, bool weak, int success, int failure);
#endif

#undef PANIC
#define PANIC() test_panic()
#define this_cpu() g_cpu
#define __atomic_compare_exchange_n test_compare_exchange
#include "../health_monitor.c"
#undef __atomic_compare_exchange_n
#include "../../sched/sched_processtimer.c"
#ifdef CONFIG_SYSTEM_REBOOT_REASON
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "../../../arch/arm/src/common/up_reboot_reason.c"
#pragma GCC diagnostic pop
#endif

static struct tcb_s g_tasks[HEALTH_MONITOR_HEAP_CAPACITY];
static struct tcb_s *g_current;
static uint32_t g_now;
static unsigned int g_irq_masked;
static unsigned int g_global_lock;
static unsigned int g_clock_reads;
static unsigned int g_try_calls;
static unsigned int g_panic_calls;
static unsigned int g_wdog_calls;
static unsigned int g_load_calls;
static bool g_advance_time;
static bool g_force_try_failure;
static void (*g_before_try)(void);
static void (*g_after_unlock)(void);
static jmp_buf g_panic_return;
static bool g_expect_panic;
#ifdef CONFIG_SYSTEM_REBOOT_REASON
static reboot_reason_code_t g_reason;
static unsigned int g_reason_writes;
#endif

static int g_logged_pid;
static unsigned long g_logged_deadline;
static unsigned int g_log_calls;

static bool registry_locked(void)
{
#ifdef CONFIG_SMP
	return __atomic_load_n(&g_health_lock, __ATOMIC_RELAXED) == SP_LOCKED;
#else
	return false;
#endif
}

void test_timeout_log(const char *format, int pid, unsigned long now, unsigned long deadline)
{
	assert(!registry_locked() && !g_global_lock);
	assert(strcmp(format, "HEALTH MONITOR TIMEOUT pid=%d now=%lu deadline=%lu\n") == 0);
	assert((int32_t)(now - deadline) >= 0);
	g_logged_pid = pid;
	g_logged_deadline = deadline;
	g_log_calls++;
}

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
	assert(__atomic_exchange_n(lock, SP_LOCKED, __ATOMIC_ACQUIRE) == SP_UNLOCKED);
}

void spin_unlock_wo_note(volatile spinlock_t *lock)
{
	assert(g_irq_masked && registry_locked());
	__atomic_store_n(lock, SP_UNLOCKED, __ATOMIC_RELEASE);
	if (g_after_unlock) {
		void (*callback)(void) = g_after_unlock;
		g_after_unlock = NULL;
		callback();
	}
}

#ifdef CONFIG_SMP
/* Interpose only the hardware CAS to count attempts/inject exclusive failure.
 * The production weak/acquire/relaxed parameters and real CAS are preserved.
 */
static bool test_compare_exchange(volatile spinlock_t *lock, spinlock_t *expected,
	spinlock_t desired, bool weak, int success, int failure)
{
	g_try_calls++;
	assert(g_irq_masked && weak);
	assert(success == __ATOMIC_ACQUIRE && failure == __ATOMIC_RELAXED);
	if (g_before_try) {
		void (*callback)(void) = g_before_try;
		g_before_try = NULL;
		callback();
	}
	if (g_force_try_failure) {
		return false;
	}
	return __atomic_compare_exchange_n(lock, expected, desired, true,
		__ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
#endif

struct tcb_s *this_task(void)
{
	assert(g_current);
	return g_current;
}

clock_t clock_systimer(void)
{
	assert(g_irq_masked);
	g_clock_reads++;
	if (g_advance_time && g_clock_reads > 2) {
		g_now += 100;
	}
	return g_now;
}

void clock_timer(void)
{
	assert(g_irq_masked && !g_global_lock);
	g_now++;
}

void sched_process_cpuload(void)
{
	g_load_calls++;
}

#ifdef CONFIG_SMP
static irqstate_t enter_critical_section(void)
{
	assert(!registry_locked());
	g_global_lock++;
	return irqsave();
}

static void leave_critical_section(irqstate_t flags)
{
	assert(g_global_lock == 1);
	g_global_lock--;
	irqrestore(flags);
}
#endif

void wd_timer(void)
{
#ifdef CONFIG_SMP
	assert(g_global_lock == 1);
#endif
	g_wdog_calls++;
}

#ifdef CONFIG_SYSTEM_REBOOT_REASON
void up_reboot_reason_write(reboot_reason_code_t reason)
{
	assert(!registry_locked() && !g_global_lock);
	g_reason = reason;
	g_reason_writes++;
}

bool up_reboot_reason_is_written(void)
{
	return g_reason != REBOOT_REASON_INITIALIZED;
}
#endif

static void test_panic(void)
{
	assert(g_expect_panic && !registry_locked() && !g_global_lock);
	assert(g_log_calls == 1);
	g_panic_calls++;
#ifdef CONFIG_SYSTEM_REBOOT_REASON
	assert(g_reason == REBOOT_SYSTEM_HEALTH_MONITOR_TIMEOUT && g_reason_writes == 1);
	/* Exercise the real common assert policy: it must preserve reason 62. */
	reboot_reason_try_write_assert(0);
	assert(g_reason == REBOOT_SYSTEM_HEALTH_MONITOR_TIMEOUT && g_reason_writes == 1);
#endif
	longjmp(g_panic_return, 1);
}

static void reset_test(uint32_t now)
{
	assert(!registry_locked() && !g_global_lock);
	g_cpu = 0;
	g_irq_masked = 0;
	g_before_try = NULL;
	g_after_unlock = NULL;
	g_force_try_failure = false;
	g_advance_time = false;
	g_health_count = 0;
	memset(g_health_heap, 0, sizeof(g_health_heap));
	memset(g_tasks, 0, sizeof(g_tasks));
	health_monitor_publish();
	g_now = now;
	g_current = &g_tasks[0];
	g_clock_reads = g_try_calls = g_panic_calls = g_wdog_calls = g_load_calls = 0;
	g_log_calls = 0;
	g_expect_panic = false;
#ifdef CONFIG_SYSTEM_REBOOT_REASON
	g_reason = REBOOT_REASON_INITIALIZED;
	g_reason_writes = 0;
#endif
}

static void start(unsigned int task, uint32_t timeout)
{
	g_current = &g_tasks[task];
	g_current->pid = task + 1;
	assert(health_monitor_start(timeout) == OK);
}

static void inspect(bool panic, bool system_tick)
{
	g_expect_panic = panic;
	g_irq_masked = 1;
	if (setjmp(g_panic_return) == 0) {
		if (system_tick) {
			sched_process_timer();
		} else {
			health_monitor_timer();
		}
		assert(!panic);
	} else {
		assert(panic);
	}
	assert(g_irq_masked && !g_global_lock);
	g_irq_masked = 0;
	g_expect_panic = false;
}

static void test_fast_paths_and_boundary(void)
{
	reset_test(100);
	inspect(false, false);
	assert(g_clock_reads == 0 && g_try_calls == 0);
	start(0, 10);
	g_now = 109;
	g_clock_reads = 0;
	inspect(false, false);
	assert(g_clock_reads == 1 && g_try_calls == 0);
	inspect(true, true); /* clock_timer advances to exactly deadline 110. */
	assert(g_now == 110 && g_panic_calls == 1);
	assert(g_load_calls == 0 && g_wdog_calls == 0);

	reset_test(100);
	start(0, 10);
	g_now = 120;
	health_monitor_kick(); /* Late, but accepted before inspection. */
	inspect(false, false);
	assert(g_health_heap[0].check_at == 130);
	g_now = 129;
	inspect(false, false);
	g_now = 130;
	inspect(true, false);
}

static void test_all_candidates(void)
{
	unsigned int index;
	uint32_t next;
	for (unsigned int same = 0; same < 2; same++) {
		reset_test(0);
		for (index = 0; index < HEALTH_MONITOR_HEAP_CAPACITY; index++) {
			start(index, same ? 256 : index + 1);
		}
		g_now = 300;
		for (index = 0; index < HEALTH_MONITOR_HEAP_CAPACITY; index++) {
			g_current = &g_tasks[index];
			health_monitor_kick();
		}
		g_clock_reads = 0;
		g_advance_time = true;
		inspect(false, false);
		g_advance_time = false;
		/* One fast-path read and one locked now, regardless of candidate count. */
		assert(g_clock_reads == 2);
		assert(health_monitor_next_check(&next) == 1);
		assert(next == (same ? 556 : 301));
		for (index = 0; index < g_health_count; index++) {
			assert(g_health_heap[index].check_at ==
				health_monitor_state(g_health_heap[index].tcb)->deadline);
		}
	}

	for (unsigned int same = 0; same < 2; same++) {
		reset_test(0);
		for (index = 0; index < HEALTH_MONITOR_HEAP_CAPACITY; index++) {
			start(index, same ? 256 : index + 1);
		}
		g_now = 300;
		for (index = 0; index + 1 < HEALTH_MONITOR_HEAP_CAPACITY; index++) {
			g_current = &g_tasks[index];
			health_monitor_kick();
		}
		inspect(true, false); /* Renewed roots cannot hide the expired target. */
		assert(g_health_heap[0].tcb == &g_tasks[HEALTH_MONITOR_HEAP_CAPACITY - 1]);
	}
}

static void test_stop_cleanup_and_wrap(void)
{
	reset_test(0);
	start(0, 1);
	g_now = 2;
	assert(health_monitor_stop() == OK);
	inspect(false, false);
	start(0, 1);
	g_now = 4;
	health_monitor_cleanup(g_current);
	inspect(false, false);

	reset_test(UINT32_MAX - 1);
	start(0, 2);
	g_now = UINT32_MAX;
	inspect(false, false);
	inspect(true, true); /* Deadline zero is valid. */

	reset_test(100);
	start(0, 1);
	g_now = 102;
	start(1, INT32_MAX);
	inspect(true, false); /* Far-future entry cannot hide an overdue root. */
}

#ifdef CONFIG_SMP
static void stop_before_lock(void)
{
	assert(health_monitor_stop() == OK);
}

static void kick_before_lock(void)
{
	health_monitor_kick();
}

static void free_after_verdict(void)
{
	/* Simulate the other CPU renewing then deleting the inspected TCB. */
	health_monitor_kick();
	health_monitor_cleanup(g_current);
	free(g_current);
	g_current = NULL;
}

static void test_interleavings(void)
{
	reset_test(0);
	start(0, 1);
	g_now = 1;
	g_cpu = 1;
	inspect(false, false);
	assert(g_try_calls == 0);
	g_cpu = 0;
	__atomic_store_n(&g_health_sequence, 3, __ATOMIC_RELAXED);
	inspect(false, false);
	assert(g_try_calls == 0);
	__atomic_store_n(&g_health_sequence, 4, __ATOMIC_RELEASE);

	__atomic_store_n(&g_health_lock, SP_LOCKED, __ATOMIC_RELAXED);
	inspect(false, false);
	assert(g_try_calls == 1 && registry_locked());
	__atomic_store_n(&g_health_lock, SP_UNLOCKED, __ATOMIC_RELEASE);
	g_force_try_failure = true;
	inspect(false, false);
	assert(g_try_calls == 2 && !registry_locked());
	g_force_try_failure = false;
	inspect(true, true);
	assert(g_try_calls == 3 && g_panic_calls == 1);

	reset_test(0);
	start(0, 1);
	g_now = 2;
	g_before_try = stop_before_lock;
	inspect(false, false); /* Cached due hint must be rechecked under lock. */
	assert(g_health_count == 0);
	start(0, 1);
	g_now = 4;
	g_before_try = kick_before_lock;
	inspect(false, false);
	assert(g_health_heap[0].check_at == 5);

	reset_test(0);
	g_current = calloc(1, sizeof(*g_current));
	assert(g_current);
	g_current->pid = 1;
	health_monitor_task_init(g_current);
	assert(health_monitor_start(1) == OK);
	g_now = 1;
	g_after_unlock = free_after_verdict;
	inspect(true, false);
	assert(g_current == NULL && g_health_count == 0 && g_panic_calls == 1);
	assert(g_logged_pid == 1 && g_logged_deadline == 1);
}
#endif

static void test_system_tick_catchup(void)
{
	reset_test(0);
	start(0, 10);
	for (unsigned int tick = 0; tick < 9; tick++) {
		inspect(false, true); /* Same repeated calls as the board's catch-up loop. */
	}
	assert(g_now == 9 && g_wdog_calls == 9 && g_load_calls == 9);
	inspect(true, true);
	assert(g_now == 10 && g_wdog_calls == 9 && g_load_calls == 9);

	reset_test(0);
	start(0, 10);
	for (unsigned int tick = 0; tick < 1000; tick++) {
		if (tick % 3 == 0) {
			health_monitor_kick();
		}
		inspect(false, true);
	}
	assert(g_now == 1000 && g_panic_calls == 0 && g_wdog_calls == 1000);
}

int main(void)
{
	test_fast_paths_and_boundary();
	test_all_candidates();
	test_stop_cleanup_and_wrap();
#ifdef CONFIG_SMP
	test_interleavings();
#endif
	test_system_tick_catchup();
	puts("PASS: timer deadlines, all candidates, wrap, fatal ordering and real system tick entry");
	return 0;
}
