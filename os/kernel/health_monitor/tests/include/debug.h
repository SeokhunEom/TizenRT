/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HEALTH_MONITOR_TEST_DEBUG_H
#define HEALTH_MONITOR_TEST_DEBUG_H
#ifdef HEALTH_MONITOR_TEST_TIMEOUT_LOG
void test_timeout_log(const char *format, int pid, unsigned long now, unsigned long deadline);
#define lldbg(...) test_timeout_log(__VA_ARGS__)
#else
#define lldbg(...) ((void)0)
#endif
#endif
