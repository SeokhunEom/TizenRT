/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HEALTH_MONITOR_TEST_INTERNAL_SCHED_H
#define HEALTH_MONITOR_TEST_INTERNAL_SCHED_H
#include <tinyara/sched.h>
struct tcb_s *this_task(void);
#ifndef this_cpu
#define this_cpu() 0
#endif
#endif
