/****************************************************************************
 *
 * Copyright 2024 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <assert.h>
#include <limits.h>
#include <tinyara/pm/pm.h>
#include <tinyara/clock.h>
#include <tinyara/irq.h>
#include <tinyara/arch.h>
#include "../kernel/sched/sched.h"
#include "../kernel/clock/clock.h"
#include "../kernel/wdog/wdog.h"
#ifdef CONFIG_HEALTH_MONITOR
#include "../kernel/health_monitor/health_monitor.h"
#endif

#include "pm.h"

#ifdef CONFIG_PM

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define PM_DEBUG_STR "."
/****************************************************************************
 * Private Variables
 ****************************************************************************/

static clock_t stime;

#ifdef CONFIG_SMP
static cpu_set_t g_active_cpu_snapshot;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: suspend_devices
 *
 * Description:
 *   This function suspend devices. It is called before entering sleep mode.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success, ERROR on failure if state change fails.
 *
 ****************************************************************************/
static int suspend_devices(void)
{
	/* Then force the global state change */
	if (pm_changestate(PM_SLEEP) < 0) {
		/* The new state change failed */
		pmlldbg("State change failed! newstate = %d\n", PM_SLEEP);
		return ERROR;
	}
	return OK;
}

/****************************************************************************
 * Name: resume_devices
 *
 * Description:
 *   This function resume devices. It is called after waking up from sleep mode.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void resume_devices(void)
{
	/* After wakeup, restore the PM state to NORMAL. */
	(void)pm_changestate(PM_NORMAL);
}

#ifdef CONFIG_SMP
/****************************************************************************
 * Name: disable_secondary_cpus
 *
 * Description:
 *   This function disables all secondary CPUs by sending a hotplug signal.
 *   It waits until each secondary CPU enters the CPU_HOTPLUG state.
 *   This is called before entering sleep mode in an SMP system.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success.
 *
 ****************************************************************************/
static int disable_secondary_cpus(void)
{
	/* Send signal to shutdown other cores here */
	for (int cpu = 1; cpu < CONFIG_SMP_NCPUS; cpu++) {
		if (CPU_ISSET(cpu, &g_active_cpu_snapshot)) {
			if (sched_cpuoff(cpu) != OK) {
				pmlldbg("CPU%d shutdown failed! Unable to shutdown secondary core for sleep mode\n", cpu);
				return ERROR;
			}
		}
	}

	return OK;
}

/****************************************************************************
 * Name: enable_secondary_cpus
 *
 * Description:
 *   This function enables all secondary CPUs after waking up from sleep.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void enable_secondary_cpus(void)
{
	for (int cpu = 1; cpu < CONFIG_SMP_NCPUS; cpu++) {
		if (CPU_ISSET(cpu, &g_active_cpu_snapshot)) {
			if (sched_cpuon(cpu) != OK) {
				pmlldbg("CPU%d power on failed!\n", cpu);
				return;
			}
		}
	}
}

/****************************************************************************
 * Name: check_secondary_cpus_idle
 *
 * Description:
 *   This function checks if all secondary CPUs are idle and ready for sleep.
 *   If any CPU is not idle, it aborts the sleep.
 *   It also saves a snapshot of active CPUs to g_active_cpu_snapshot,
 *   which is later used by disable/enable_secondary_cpus functions.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK if all secondary CPUs are idle, ERROR if sleep should be aborted.
 *
 ****************************************************************************/
static int check_secondary_cpus_idle(void)
{
	int cpu;
	FAR struct tcb_s *tcb;

	/* For SMP case, we need to check secondary core status
	 * If secondary core status is not in idle thread, abort
	 * the sleep and check again on next cycle
	 */

	g_active_cpu_snapshot = sched_getactivecpu();

	for (cpu = 1; cpu < CONFIG_SMP_NCPUS; cpu++) {
		if (!CPU_ISSET(cpu, &g_active_cpu_snapshot)) {
			continue;
		}

		tcb = current_task(cpu);
		/* Check if current cpu is in idle thread */
		if (tcb->pid != cpu) {
			pmllvdbg("Sleep abort! CPU%d task: %s!\n", cpu, tcb->name);
			return ERROR;
		}
	}

	return OK;
}
#else
#define disable_secondary_cpus()    (OK)
#define enable_secondary_cpus()
#define check_secondary_cpus_idle() (OK)
#endif

/****************************************************************************
 * Name: disable_systick
 *
 * Description:
 *   This function disables the system tick timer before entering sleep.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success.
 *
 ****************************************************************************/
static int disable_systick(void)
{
	return up_timer_disable();
}

/****************************************************************************
 * Name: enable_and_compensate_systick
 *
 * Description:
 *   This function enables the system tick timer after waking up and
 *   compensates for any ticks missed during sleep. It updates the
 *   system time and timer accordingly.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void enable_and_compensate_systick(bool transition, bool slept)
{
#ifdef CONFIG_PM_TICKSUPPRESS
	struct pm_sleep_ops *sleep_ops = g_pmglobals.sleep_ops;
	clock_t missing_tick;

	if (!sleep_ops || (!transition && (!slept || !sleep_ops->get_missingtick))) {
		goto enable_timer;
	}

	missing_tick = transition ? sleep_ops->get_elapsedtick() : sleep_ops->get_missingtick();

	pmllvdbg("missing_tick: %llu\n", missing_tick);
	pm_metrics_update_missing_tick(missing_tick);

	if (missing_tick > 0) {
		/* Correcting for missed system ticks in sleep. */
		clock_timer_nohz(missing_tick);

		/* Compensate wd timer for missing ticks by pm sleep.
		 * But to guarantee fast execution of interrupt service routine after wakeup,
		 * expiration of wd_timer is not done here
		 *
		 *     WAKE UP -> HW IRQ ISR -> THREAD -> TICK ISR
		 *           |              |          |           |
		 *           +--------------+----------+-----------+
		 *     (corrects tick)                       (expire timer)
		 */
		wd_timer_nohz(missing_tick);
	}

enable_timer:
#else
	(void)transition;
	(void)slept;
#endif
	(void)up_timer_enable();
}

#if defined(CONFIG_PM_TIMEDWAKEUP) || defined(CONFIG_HEALTH_MONITOR) || \
	(defined(CONFIG_WATCHDOG_FOR_IRQ) && defined(CONFIG_ARCH_HAVE_WDOG_WAKEUP))
/****************************************************************************
 * Name: get_next_wakeup_time
 *
 * Description:
 *   Select the earliest software watchdog, monitor or HW watchdog wakeup.
 *   If the delay is too short (less than SLEEP_ENTRY_WAIT), it returns ERROR
 *   to abort sleep.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Positive delay in ticks, zero for no reservation, or ERROR to defer sleep.
 *
 ****************************************************************************/
static int get_next_wakeup_time(clock_t elapsed)
{
	clock_t delay = 0;
	(void)elapsed;

#ifdef CONFIG_PM_TIMEDWAKEUP
	/* get wakeup timer */
	delay = wd_getwakeupdelay();
	if (delay > 0) {
		if (elapsed >= delay) {
			return ERROR;
		}
		delay -= elapsed;
	}
#endif
#ifdef CONFIG_HEALTH_MONITOR
	uint32_t check_at;
	int status = health_monitor_next_check(&check_at);

	/* PM holds the scheduler lock. Read only the published hint; never
	 * acquire the registry lock or wait for an in-progress publication.
	 */
	if (status < 0) {
		return ERROR;
	}
	if (status > 0) {
#if defined(CONFIG_PM_TIMEDWAKEUP) && defined(CONFIG_PM_TICKSUPPRESS)
		struct pm_sleep_ops *sleep_ops = g_pmglobals.sleep_ops;
		int32_t remaining = (int32_t)(check_at - (uint32_t)clock_systimer());

		/* Tick zero is a valid reservation, and a due reservation must not
		 * be mistaken for no wakeup. Sleep must preserve elapsed time too.
		 */
		if (remaining <= 0 || elapsed >= (clock_t)remaining ||
			!sleep_ops || !sleep_ops->sleep || !sleep_ops->set_timer ||
			!sleep_ops->get_elapsedtick) {
			return ERROR;
		}
		remaining -= elapsed;
		if (delay == 0 || (clock_t)remaining < delay) {
			delay = remaining;
		}
#else
		/* Active monitoring needs timed wakeup and sleep-time accounting. */
		return ERROR;
#endif
	}
#endif

#if defined(CONFIG_WATCHDOG_FOR_IRQ) && defined(CONFIG_ARCH_HAVE_WDOG_WAKEUP)
	int wdog_delay = up_wdog_getwakeupdelay();

	if (wdog_delay < 0) {
		return ERROR;
	}
	if (wdog_delay > 0) {
#if defined(CONFIG_PM_TIMEDWAKEUP) && defined(CONFIG_PM_TICKSUPPRESS)
		struct pm_sleep_ops *sleep_ops = g_pmglobals.sleep_ops;
		if (!sleep_ops || !sleep_ops->sleep || !sleep_ops->set_timer ||
			(!sleep_ops->get_elapsedtick && !sleep_ops->get_missingtick)) {
			return ERROR;
		}
		if (delay == 0 || (clock_t)wdog_delay < delay) {
			delay = wdog_delay;
		}
#else
		/* An unstoppable HW watchdog cannot allow untimed sleep. */
		return ERROR;
#endif
	}
#endif

#ifdef CONFIG_PM_TIMEDWAKEUP
	if ((delay > 0) && (delay < MSEC2TICK(CONFIG_PM_SLEEP_ENTRY_WAIT_MS))) {
		pmllvdbg("Wdog Timer Delay: %ldms is less than SLEEP_ENTRY_WAIT: %ldms\n", TICK2MSEC(delay), CONFIG_PM_SLEEP_ENTRY_WAIT_MS);
		return ERROR;
	}
#endif

	/* The caller uses a signed result so ERROR remains distinct. A long
	 * watchdog delay may exceed int even though monitor delays cannot.
	 */
	return delay > INT_MAX ? INT_MAX : (int)delay;
}
#else
#define get_next_wakeup_time(elapsed) (0)
#endif

#ifdef CONFIG_PM_TIMEDWAKEUP
/****************************************************************************
 * Name: set_pm_wakeup_timer
 *
 * Description:
 *   This function sets the wakeup timer to wake up the system after
 *   the specified delay. It uses the sleep_ops to set the timer.
 *
 * Input Parameters:
 *   delay - The wakeup delay in ticks.
 *
 * Returned Value:
 *   OK on success, or the error code from sleep_ops->set_timer on failure.
 *
 ****************************************************************************/
static int set_pm_wakeup_timer(int delay) 
{
	if (delay > 0) {
		/* Board timers accept unsigned microseconds. Clamp long delays
		 * to an early wakeup instead of overflowing the conversion.
		 */
		uint64_t delay_us = (uint64_t)delay * USEC_PER_TICK;
		if (delay_us > UINT_MAX) {
			delay_us = UINT_MAX;
		}
		pmllvdbg("Setting timer and board will wake up after %ld millisecond\n", delay);
		return g_pmglobals.sleep_ops->set_timer((unsigned int)delay_us);
	}

	return OK;
}
#else
#define set_pm_wakeup_timer(delay)  (OK)
#endif

/****************************************************************************
 * Name: update_wakeup_reason
 *
 * Description:
 *   This function updates the wakeup reason by querying the sleep_ops.
 *   It logs the wakeup source and updates PM metrics.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void update_wakeup_reason(void)
{
	pm_wakeup_reason_code_t wakeup_src;
	struct pm_sleep_ops *sleep_ops = g_pmglobals.sleep_ops;

	if (sleep_ops && sleep_ops->get_wakeupreason) {
		wakeup_src = sleep_ops->get_wakeupreason();
		if (wakeup_src >= PM_WAKEUP_SRC_COUNT) {
			wakeup_src = PM_WAKEUP_UNKNOWN;
		}

		lldbg_noarg(": %s", wakeup_src_name[wakeup_src]);
		pm_metrics_update_wakeup_reason(wakeup_src);
	}
}

/****************************************************************************
 * Name: check_pm_state
 *
 * Description:
 *   This function checks if the system can enter sleep mode.
 *   It verifies the PM state and checks if all secondary CPUs are idle.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK if the system can enter sleep, ERROR if sleep should be aborted.
 *
 ****************************************************************************/
static int check_pm_state(void) 
{
	/* Decide, which power saving level can be obtained */
	if (pm_checkstate() != PM_SLEEP) {
		return ERROR;
	}

	if (check_secondary_cpus_idle() != 0) {
		return ERROR;
	}

	return OK;
}

/****************************************************************************
 * Name: enter_sleep
 *
 * Description:
 *   This function handles the entire sleep sequence. It gets the wakeup time,
 *   suspends devices, disables secondary CPUs and systick, sets the wakeup
 *   timer, enters sleep, and then resumes everything on wakeup.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void enter_sleep(void)
{
	int next_wakeup_time;
	bool transition = false;
	bool slept = false;
	struct pm_sleep_ops *sleep_ops = g_pmglobals.sleep_ops;

	DEBUGASSERT(sleep_ops);

	next_wakeup_time = get_next_wakeup_time(0);
	if (next_wakeup_time < 0) {
		return;
	}

#ifdef CONFIG_PM_TICKSUPPRESS
	/* The board clock covers preparation and all abort paths, not just sleep. */
	transition = sleep_ops->get_elapsedtick != NULL;
	if (transition && disable_systick() != OK) {
		enable_and_compensate_systick(true, false);
		return;
	}
#endif

	lldbg_noarg(PM_DEBUG_STR);
	if (suspend_devices() != OK) {
		goto DEVICES_RESUME;
	}

	lldbg_noarg(PM_DEBUG_STR);
	if (disable_secondary_cpus() != OK) {
		goto CPUS_ENABLE;
	}

	/* Re-read the hint after quiescing other CPUs. OS ticks are frozen;
	 * subtract the board's elapsed interval without changing the registry.
	 * The HW watchdog already measures its own elapsed hardware time.
	 */
	next_wakeup_time = get_next_wakeup_time(transition ? sleep_ops->get_elapsedtick() : 0);
	if (next_wakeup_time < 0) {
		goto CPUS_ENABLE;
	}

	if (!transition && disable_systick() != OK) {
		goto SYSTICK_ENABLE;
	}

	if (set_pm_wakeup_timer(next_wakeup_time) != OK) {
		goto SYSTICK_ENABLE;
	}

	lldbg_noarg(PM_DEBUG_STR);
	slept = sleep_ops->sleep != NULL;
	if (slept && sleep_ops->sleep() != 0) {
		goto SYSTICK_ENABLE;
	}

	update_wakeup_reason();

SYSTICK_ENABLE:
	if (!transition) {
		enable_and_compensate_systick(false, slept);
	}
	lldbg_noarg(PM_DEBUG_STR);

CPUS_ENABLE:
	enable_secondary_cpus();
	lldbg_noarg(PM_DEBUG_STR);

DEVICES_RESUME:
	resume_devices();
	if (transition) {
		enable_and_compensate_systick(true, slept);
	}
	lldbg_noarg(PM_DEBUG_STR"\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pm_idle
 *
 * Description:
 *   This function is called by IDLE thread to make board sleep. This function
 *   also allow to set wake up timer & handler and do all the PM pre processing
 *   required before going to sleep.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void pm_idle(void)
{
	irqstate_t flags;
	clock_t now;
	/* State change only if PM is ready to state change */
	if (!g_pmglobals.is_running) {
		return;
	}

	now = clock_systimer();
	/* We need to check and change PM state transition only if one tick time has been passed,
	 * because state transition only happens when CPU receive TICK INTERRUPT. So checking pm state
	 * multiple times within one tick is waste of CPU clocks and we should avoid it.
	 */
	if (now <= stime) {
		return;
	}	
	
	flags = enter_critical_section();
	sched_lock();

	pm_metrics_update_idle();

	if (check_pm_state() == OK) {
		enter_sleep();
	}

	stime = clock_systimer();

	sched_unlock();
	leave_critical_section(flags);
}

#endif /* CONFIG_PM */
