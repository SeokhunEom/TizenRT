/* SPDX-License-Identifier: Apache-2.0 */
/* Host-only configuration for the registry tests, not a board configuration. */
#ifndef HEALTH_MONITOR_TEST_CONFIG_H
#define HEALTH_MONITOR_TEST_CONFIG_H
#define CONFIG_HEALTH_MONITOR 1
#define CONFIG_MAX_TASKS 256
#define CONFIG_USEC_PER_TICK 1000
#define CONFIG_HAVE_LONG_LONG 1
#define CONFIG_SYSTEM_TIME64 1
#ifndef HEALTH_MONITOR_TEST_UP
#define CONFIG_SMP 1
#define CONFIG_SMP_NCPUS 2
#else
#define CONFIG_SMP_NCPUS 1
#endif
#define OK 0
#endif
