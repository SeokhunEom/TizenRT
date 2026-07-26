/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
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

/// @file tc_internal.h

/// @brief Header file for Kernel TestCase Example
#ifndef __EXAMPLES_TESTCASE_KERNEL_TC_INTERNAL_H
#define __EXAMPLES_TESTCASE_KERNEL_TC_INTERNAL_H

#include "tc_common.h"

#ifdef CONFIG_CPP_HAVE_VARARGS

#ifdef CONFIG_DEBUG_TC_KN
# define tckndbg(format, ...)    dbg(format, ##__VA_ARGS__)
# define tcknlldbg(format, ...)  lldbg(format, ##__VA_ARGS__)
# define tcknvdbg(format, ...)   vdbg(format, ##__VA_ARGS__)
# define tcknllvdbg(format, ...) llvdbg(format, ##__VA_ARGS__)
#else
# define tckndbg(...)
# define tcknlldbg(...)
# define tcknvdbg(...)
# define tcknllvdbg(...)
#endif

#else /* ! CONFIG_CPP_HAVE_VARARGS */

#ifdef CONFIG_DEBUG_TC_KN
# define tckndbg       dbg
# define tcknlldbg     lldbg
# define tcknvdbg      vdbg
# define tcknllvdbg    llvdbg
#else
# define tckndbg       (void)
# define tcknlldbg     (void)
# define tcknvdbg      (void)
# define tcknllvdbg    (void)
#endif

#endif /* CONFIG_CPP_HAVE_VARARGS */

/**********************************************************
* TC Function Declarations
**********************************************************/
int clock_main(void);
void clock_conversion_main(void);
int binfmt_main(void);
int binary_manager_main(void);
int environ_main(void);
int errno_main(void);
int group_main(void);
void group_signal_main(void);
int log_dump_main(void);
int libc_fixedmath_main(void);
int libc_inttypes_main(void);
int libc_libgen_main(void);
int libc_math_main(void);
int libc_misc_main(void);
int libc_mqueue_main(void);
int libc_pthread_main(void);
int libc_queue_main(void);
int libc_sched_main(void);
int libc_semaphore_main(void);
int libc_signal_main(void);
int libc_stdio_main(void);
int libc_stdlib_main(void);
int libc_string_main(void);
int libc_timer_main(void);
int libc_unistd_main(void);
int libc_syslog_main(void);
int mqueue_main(void);
int kernel_net_pbuf_main(void);
int pipe_main(void);
int pm_tc_main(void);
int procfs_main(void);
int pthread_main(void);
void pthread_kernel_main(void);
int rtc_main(void);
int roundrobin_main(void);
int sched_main(void);
void sched_foreach_main(void);
void sched_affinity_main(void);
void sched_state_main(void);
int semaphore_main(void);
void semaphore_kernel_main(void);
int signal_main(void);
void signal_findaction_null_main(void);
void signal_pendingset_main(void);
int debug_main(void);
int task_main(void);
void task_lifecycle_main(void);
#if defined(CONFIG_TC_KERNEL_TASK) && defined(CONFIG_SCHED_STARTHOOK)
void task_starthook_main(void);
#endif
int termios_main(void);
int timer_tc_main(void);
void timer_deleteall_main(void);
int vfs_main(void);
int wdog_main(void);
int umm_heap_main(void);
int tash_heapinfo_main(void);
int tash_stackmonitor_main(void);
int wqueue_main(void);
int memory_safety_main(void);
int irq_main(void);
void irq_kernel_main(void);
int mem_leak_checker_main(void);
int reboot_reason_main(void);
int itc_environ_main(void);
int itc_libc_pthread_main(void);
int itc_libc_semaphore_main(void);
int itc_semaphore_main(void);
int itc_sched_main(void);
int itc_timer_main(void);
int itc_pthread_main(void);
int tc_get_drvfd(void);
int tc_run_os_api_ioctl(int fd, int request);

#endif /* __EXAMPLES_TESTCASE_KERNEL_TC_INTERNAL_H */
