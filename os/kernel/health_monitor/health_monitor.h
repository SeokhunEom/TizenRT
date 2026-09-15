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

#define HEALTH_MONITOR_HEAP_CAPACITY     CONFIG_MAX_TASKS
#define HEALTH_MONITOR_MAX_TIMEOUT_TICKS INT32_MAX

/* KICK changes only the monitored deadline. The heap retains the earlier check_at
 * until the timer inspects the entry. Storage is allocated statically by
 * the implementation, with one entry per possible registered thread.
 */

struct health_monitor_entry_s {
	FAR struct tcb_s *tcb;
	uint32_t check_at;
};

/* Resolve state only here so storage can later move out of the TCB. The
 * caller supplies a live TCB with an assigned PID and protects state access
 * with the health monitor lock. This helper only computes an address; it
 * neither locks nor allocates. Retain the TCB, not the returned state
 * pointer, across operations.
 */

static inline FAR struct health_monitor_s *health_monitor_state(FAR struct tcb_s *tcb)
{
	return &tcb->health_monitor;
}

/* Convert with a 64-bit intermediate to avoid overflow and round up, rather
 * than using MSEC2TICK's nearest-tick rounding. Zero denotes an invalid
 * timeout and is reserved for the unregistered TCB state.
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

/* Read time as (uint32_t)clock_systimer(), including PM compensation. Direct
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

/* START and STOP return zero or a negative errno. KICK is a no-op if
 * unregistered. Their implementations are added with the registry.
 */

int health_monitor_start(uint32_t timeout_ms);
void health_monitor_kick(void);
int health_monitor_stop(void);

/* Clear registration and remove the heap entry before the PID is released
 * or the TCB is freed. Task restart also clears registration. Repeated
 * cleanup of the same live TCB is safe.
 */

void health_monitor_cleanup(FAR struct tcb_s *tcb);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_HEALTH_MONITOR */
#endif /* __KERNEL_HEALTH_MONITOR_HEALTH_MONITOR_H */
