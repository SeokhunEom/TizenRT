/* SPDX-License-Identifier: Apache-2.0 */
/* Host TCB model. Real board layout is verified by the ARM kernel build. */
#ifndef HEALTH_MONITOR_TEST_SCHED_H
#define HEALTH_MONITOR_TEST_SCHED_H
#include <stdint.h>
#include <sys/types.h>
struct health_monitor_s {
	uint32_t deadline;
	uint32_t timeout;
};
struct tcb_s {
	pid_t pid;
	struct health_monitor_s health_monitor;
};
#endif
