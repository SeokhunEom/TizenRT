/* SPDX-License-Identifier: Apache-2.0 */
/* Host PM/scheduler/board fixture. Production pm_idle.c is included unchanged.
 * Suppress its relative private headers; the target build checks real types.
 */
#ifndef HEALTH_MONITOR_TEST_PM_H
#define HEALTH_MONITOR_TEST_PM_H
#define __DRIVERS_POWER_PM_H
#define __SCHED_SCHED_SCHED_H
#define __SCHED_CLOCK_CLOCK_H
#define __SCHED_WDOG_WDOG_H
#include <stdbool.h>
#include <sys/types.h>
#include <tinyara/spinlock.h>
#include <tinyara/sched.h>

#define ERROR (-1)
#define DEBUGASSERT(condition) assert(condition)
#define pmlldbg(...) ((void)0)
#define pmllvdbg(...) ((void)0)
#define lldbg_noarg(...) ((void)0)
#define pm_metrics_update_idle() ((void)0)
#define pm_metrics_update_missing_tick(ticks) ((void)(ticks))
#define pm_metrics_update_wakeup_reason(reason) ((void)(reason))

typedef unsigned int test_cpu_set_t;
#define cpu_set_t test_cpu_set_t
#define CPU_ISSET(cpu, mask) ((*(mask) & (1u << (cpu))) != 0)

enum pm_state_e {
	PM_NORMAL,
	PM_SLEEP
};
typedef enum {
	PM_WAKEUP_UNKNOWN,
	PM_WAKEUP_SRC_COUNT
} pm_wakeup_reason_code_t;
struct pm_sleep_ops {
	int (*sleep)(void);
	int (*set_timer)(unsigned int delay_us);
	pm_wakeup_reason_code_t (*get_wakeupreason)(void);
	clock_t (*get_missingtick)(void);
	clock_t (*get_elapsedtick)(void);
};
struct pm_global_s {
	bool is_running;
	struct pm_sleep_ops *sleep_ops;
};
extern struct pm_global_s g_pmglobals;
int pm_changestate(enum pm_state_e newstate);
enum pm_state_e pm_checkstate(void);
irqstate_t enter_critical_section(void);
void leave_critical_section(irqstate_t flags);
int sched_lock(void);
int sched_unlock(void);
cpu_set_t sched_getactivecpu(void);
struct tcb_s *current_task(int cpu);
int sched_cpuoff(int cpu);
int sched_cpuon(int cpu);
int up_timer_disable(void);
int up_timer_enable(void);
clock_t wd_getwakeupdelay(void);
void clock_timer_nohz(clock_t ticks);
void wd_timer_nohz(clock_t ticks);
#endif
