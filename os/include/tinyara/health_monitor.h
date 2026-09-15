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

#ifndef __INCLUDE_TINYARA_HEALTH_MONITOR_H
#define __INCLUDE_TINYARA_HEALTH_MONITOR_H

#include <tinyara/fs/ioctl.h>

#define HEALTH_MONITOR_DEVPATH        "/dev/health_monitor"
#define HEALTH_MONITOR_MIN_TIMEOUT_MS 1U

/* Each command operates on the calling thread, even when an fd is shared.
 * Opening the device does not register a thread. Closing it does not stop
 * monitoring; use HMIOC_STOP or thread exit to release the registration.
 */

/* START: arg is a uint32_t timeout in milliseconds, passed by value as an
 * unsigned long, not a pointer. Round up to system ticks. Valid timeouts
 * convert to 1..INT32_MAX ticks (1..2147483647 ms with a 1 ms tick).
 * Returns EINVAL for an invalid timeout and EEXIST if already registered.
 */

#define HMIOC_START _HMIOC(0x0001)

/* KICK: arg is zero. Refresh the deadline without checking the old deadline.
 * A kick accepted before inspection may renew an already elapsed deadline.
 * Kicking an unregistered thread succeeds without changing state.
 */

#define HMIOC_KICK  _HMIOC(0x0002)

/* STOP: arg is zero. Unregister the calling thread, or return ENOENT if it
 * is not registered. Unknown commands return ENOTTY. Successful commands
 * return zero; failures use the usual ioctl return value and errno rules.
 */

#define HMIOC_STOP  _HMIOC(0x0003)

#endif /* __INCLUDE_TINYARA_HEALTH_MONITOR_H */
