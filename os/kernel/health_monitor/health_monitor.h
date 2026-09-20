/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#ifndef __KERNEL_HEALTH_MONITOR_HEALTH_MONITOR_H
#define __KERNEL_HEALTH_MONITOR_HEALTH_MONITOR_H

#include <tinyara/config.h>

#ifdef CONFIG_HEALTH_MONITOR

#include <stdbool.h>
#include <stdint.h>
#include <tinyara/compiler.h>
#include <tinyara/clock.h>
#include <tinyara/health_monitor.h>
#include <tinyara/sched.h>

#if defined(CONFIG_EXAMPLES_HEALTH_MONITOR_QEMU) && defined(CONFIG_QEMU_HEALTH_MONITOR_CAPACITY)
#define HEALTH_MONITOR_HEAP_CAPACITY     CONFIG_QEMU_HEALTH_MONITOR_CAPACITY
#else
#define HEALTH_MONITOR_HEAP_CAPACITY     CONFIG_MAX_TASKS
#endif
#define HEALTH_MONITOR_MAX_TIMEOUT_TICKS INT32_MAX

/* KICK changes only the monitored deadline. The heap retains the earlier check_at
 * until the timer inspects the entry. Storage is allocated statically by
 * the implementation, with one entry per possible registered thread.
 */

struct health_monitor_entry_s {
	FAR struct tcb_s *tcb;
	uint32_t check_at;
};

/* Resolve the embedded TCB health state in O(1).
 * The caller supplies a live TCB with an assigned PID and protects access
 * with the health monitor lock. This helper only computes an address; it
 * neither locks nor allocates. Retain the TCB, not the returned state
 * pointer, across operations.
 */

static inline FAR struct health_monitor_s *health_monitor_state(FAR struct tcb_s *tcb)
{
	return &tcb->health_monitor;
}

/* Convert a requested duration in milliseconds to ticks, in O(1).
 * Reject values below the minimum; multiply by microseconds per millisecond
 * in 64 bits, add tick_us - 1 and divide by tick_us to round UP. This avoids
 * overflow and never shortens the requested duration through rounding,
 * unlike MSEC2TICK's nearest-tick rounding.
 *
 * Return 1..INT32_MAX ticks, or zero if the requested duration is invalid
 * or too large for the signed half-range time comparisons. Zero remains
 * reserved for the unregistered state. This is pure arithmetic: it does
 * not read the current tick, modify state or acquire a lock.
 */

static inline uint32_t health_monitor_timeout_ticks(uint32_t timeout_ms)
{
	uint64_t ticks;

	if (timeout_ms < HEALTH_MONITOR_MIN_TIMEOUT_MS) {
		return 0;
	}

	ticks = ((uint64_t)timeout_ms * USEC_PER_MSEC + USEC_PER_TICK - 1) / USEC_PER_TICK;
	if (ticks > HEALTH_MONITOR_MAX_TIMEOUT_TICKS) {
		return 0;
	}

	return (uint32_t)ticks;
}

/* Test whether lhs is strictly before rhs in wrapping 32-bit tick time.
 * Subtract as unsigned ticks, then interpret the difference as signed;
 * this also works across UINT32_MAX -> 0. Equal instants return false.
 * Used for pairwise time tests, not arbitrary heap-key ordering.
 *
 * Read time as (uint32_t)clock_systimer(), including PM compensation. Direct
 * comparisons are unambiguous only for instants less than 2^31 ticks apart.
 * Zero is a valid tick value. For heap ordering, compare signed offsets
 * from one common now: an overdue key and a far-future key can otherwise
 * be more than INT32_MAX ticks apart. Inspect before an overdue key is
 * 2^31 ticks old; delays beyond that are outside the supported time range.
 */

static inline bool health_monitor_tick_before(uint32_t lhs, uint32_t rhs)
{
	return (int32_t)(lhs - rhs) < 0;
}

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize a new, not-yet-runnable TCB after PID assignment. This is not
 * an alternative to cleanup for a registered or restarting task.
 */

void health_monitor_task_init(FAR struct tcb_s *tcb);

/* START and STOP return zero or a negative errno. KICK is a no-op if
 * unregistered. Call these operations only from thread context.
 */

int health_monitor_start(uint32_t timeout_ms);
void health_monitor_kick(void);
int health_monitor_stop(void);

/* Clear registration and remove the heap entry before the PID is released
 * or the TCB is freed. Task restart also clears registration. Repeated
 * cleanup of the same live TCB is safe.
 */

void health_monitor_cleanup(FAR struct tcb_s *tcb);

/* Read the cached earliest reservation without locking or dereferencing a
 * TCB. Returns 1 and writes check_at for a stable nonempty snapshot, 0 for
 * an empty snapshot, or -EAGAIN if the attempt detects publication overlap.
 * The output is unchanged for 0/-EAGAIN. Zero itself is a valid check_at.
 *
 * This is a scheduling hint, not a timeout verdict. On -EAGAIN callers
 * must defer inspection/sleep rather than treating the monitor as empty.
 * The timer and PM wakeup selection both use this hint.
 */

int health_monitor_next_check(FAR uint32_t *check_at);

/* CPU0 tick hook, after clock_timer() and before scheduler global locks.
 * Inspect due reservations without waiting for the registry lock. On an
 * expired latest deadline, unlock and enter the existing fatal path.
 * Call only from the system timer interrupt with local IRQs disabled.
 * Return true for a completed non-expiring check, false on CPU1 or a
 * deferred check. Only a true result permits the tick's HW WDT refresh.
 */

bool health_monitor_timer(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_HEALTH_MONITOR */
#endif /* __KERNEL_HEALTH_MONITOR_HEALTH_MONITOR_H */
