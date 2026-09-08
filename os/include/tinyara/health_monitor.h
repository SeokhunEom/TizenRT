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
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
#ifndef __INCLUDE_TINYARA_HEALTH_MONITOR_H
#define __INCLUDE_TINYARA_HEALTH_MONITOR_H

#include <tinyara/config.h>
#include <stdint.h>

#ifdef CONFIG_HEALTH_MONITOR
#ifdef __cplusplus
extern "C" {
#endif

/* Self-only, task-context APIs. Return 0 or a negative errno (no errno write).
 * No allocation, blocking synchronization, scheduler lock or I/O. Not signal
 * safe. One contract per task; stop before cooperative exit. Timeout is in ms.
 * kick declares meaningful progress, not merely that the task was scheduled.
 * -EAGAIN: concurrent clock publication, no state change; retry later.
 * -ENOSPC: local membership queue full, no state change; retry later.
 * Never retry in a spin loop. A failed stop remains armed.
 * -EIO/-EOVERFLOW: publication/counter failure reported to CPU0.
 * CPU0 owns final fault confirmation; concurrent calls may complete before
 * observing shutdown. See docs/health_monitor.md for boundary semantics.
 */
int health_monitor_start(uint32_t timeout_ms);
int health_monitor_stop(void);
int health_monitor_kick(void);

#ifdef __cplusplus
}
#endif
#endif
#endif
