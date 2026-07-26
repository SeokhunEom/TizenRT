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

/* This registry owns command IDs 29..255 added by the os_api_test
 * remediation series; commands 1..28 stay in their legacy definitions and
 * dispatch blocks.  Each provider adds one row and every named topology
 * dimension atomically:
 *
 * #if defined(CONFIG_TC_KERNEL_NAME)
 * OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_NAME, 29, test_name, test_name.c, name_main, tc_name.c, CONFIG_TC_KERNEL_NAME)
 * #endif
 *
 * Rows are single-line macro invocations.  A predicate is a space-separated
 * conjunction of defined(CONFIG_NAME) or !defined(CONFIG_NAME) atoms, and must
 * positively include defined(test_gate).  The fields are ioctl
 * symbol, numeric command ID, provider function/source, wrapper function/source,
 * and testcase Kconfig gate, in that order.
 */

#if defined(CONFIG_TC_KERNEL_UMM_HEAP) && defined(CONFIG_MM_KERNEL_HEAP)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_KMM_HEAP_TEST, 29, test_kmm, test_kmm.c, umm_heap_main, tc_umm_heap.c, CONFIG_TC_KERNEL_UMM_HEAP)
#endif
#if defined(CONFIG_TC_KERNEL_TASK) && defined(CONFIG_SCHED_STARTHOOK) && defined(CONFIG_BUILD_PROTECTED)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_TASK_LIFECYCLE_TEST, 30, test_task, test_task.c, task_lifecycle_main, tc_task.c, CONFIG_TC_KERNEL_TASK)
#endif
#if defined(CONFIG_TC_KERNEL_SCHED)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_SCHED_FOREACH_TEST, 31, test_sched, test_sched.c, sched_foreach_main, tc_sched.c, CONFIG_TC_KERNEL_SCHED)
#endif
#if defined(CONFIG_TC_KERNEL_SIGNAL) && !defined(CONFIG_DISABLE_SIGNALS)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_SIG_FINDACTION_NULL_TEST, 32, test_signal, test_signal.c, signal_findaction_null_main, tc_signal.c, CONFIG_TC_KERNEL_SIGNAL)
#endif
#if defined(CONFIG_TC_KERNEL_CLOCK)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_CLOCK_CONVERSION_TEST, 33, test_clock, test_clock.c, clock_conversion_main, tc_clock.c, CONFIG_TC_KERNEL_CLOCK)
#endif
#if defined(CONFIG_TC_KERNEL_MQUEUE) && !defined(CONFIG_DISABLE_MQUEUE)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_MQUEUE_TEST, 36, test_mqueue, test_mqueue.c, mqueue_main, tc_mqueue.c, CONFIG_TC_KERNEL_MQUEUE)
#endif
#if defined(CONFIG_TC_KERNEL_TIMER) && !defined(CONFIG_DISABLE_POSIX_TIMERS)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_TIMER_DELETEALL_TEST, 34, test_timer, test_timer.c, timer_deleteall_main, tc_timer.c, CONFIG_TC_KERNEL_TIMER)
#endif
#if defined(CONFIG_TC_KERNEL_SEMAPHORE)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_SEM_KERNEL_TEST, 37, test_sem, test_sem.c, semaphore_kernel_main, tc_semaphore.c, CONFIG_TC_KERNEL_SEMAPHORE)
#endif
#if defined(CONFIG_TC_KERNEL_ENVIRON) && !defined(CONFIG_DISABLE_ENVIRON)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_ENVIRON_TEST, 38, test_environ, test_environ.c, environ_main, tc_environ.c, CONFIG_TC_KERNEL_ENVIRON)
#endif
#if defined(CONFIG_TC_KERNEL_ERRNO)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_ERRNO_TEST, 39, test_errno, test_errno.c, errno_main, tc_errno.c, CONFIG_TC_KERNEL_ERRNO)
#endif
#if defined(CONFIG_TC_KERNEL_PTHREAD) && !defined(CONFIG_DISABLE_PTHREAD)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_PTHREAD_TEST, 40, test_pthread, test_pthread.c, pthread_kernel_main, tc_pthread.c, CONFIG_TC_KERNEL_PTHREAD)
#endif
#if defined(CONFIG_TC_KERNEL_IRQ)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_IRQ_TEST, 41, test_irq, test_irq.c, irq_kernel_main, tc_irq.c, CONFIG_TC_KERNEL_IRQ)
#endif
#if defined(CONFIG_TC_KERNEL_PROCFS) && defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_UPTIME) && !defined(CONFIG_FS_PROCFS_EXCLUDE_VERSION)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_PROCFS_TEST, 47, test_procfs, test_procfs.c, procfs_main, tc_procfs.c, CONFIG_TC_KERNEL_PROCFS)
#endif
#if defined(CONFIG_TC_KERNEL_PIPE) && defined(CONFIG_PIPES)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_PIPE_TEST, 49, test_pipe, test_pipe.c, pipe_main, tc_pipe.c, CONFIG_TC_KERNEL_PIPE)
#endif
#if defined(CONFIG_TC_KERNEL_VFS)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_VFS_TEST, 51, test_vfs, test_vfs.c, vfs_main, tc_vfs.c, CONFIG_TC_KERNEL_VFS)
#endif
#if defined(CONFIG_TC_KERNEL_TERMIOS) && defined(CONFIG_SERIAL_TERMIOS)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_TERMIOS_TEST, 52, test_termios, test_termios.c, termios_main, tc_termios.c, CONFIG_TC_KERNEL_TERMIOS)
#endif
#if defined(CONFIG_TC_KERNEL_SCHED)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_SCHED_AFFINITY_TEST, 53, test_sched, test_sched.c, sched_affinity_main, tc_sched.c, CONFIG_TC_KERNEL_SCHED)
#endif
#if defined(CONFIG_TC_KERNEL_GROUP) && defined(CONFIG_SCHED_HAVE_PARENT) && defined(CONFIG_SCHED_CHILD_STATUS) && !defined(CONFIG_DISABLE_SIGNALS)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_GROUP_SIGNAL_TEST, 54, test_group, test_group.c, group_signal_main, tc_group.c, CONFIG_TC_KERNEL_GROUP)
#endif
#if defined(CONFIG_TC_KERNEL_TASK) && defined(CONFIG_SCHED_STARTHOOK)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_TASK_STARTHOOK_TEST, 55, test_task, test_task.c, task_starthook_main, tc_task.c, CONFIG_TC_KERNEL_TASK)
#endif
#if defined(CONFIG_TC_KERNEL_SCHED)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_SCHED_STATE_TEST, 56, test_sched, test_sched.c, sched_state_main, tc_sched.c, CONFIG_TC_KERNEL_SCHED)
#endif
#if defined(CONFIG_TC_KERNEL_SIGNAL) && !defined(CONFIG_DISABLE_SIGNALS)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_SIG_PENDINGSET_TEST, 57, test_signal, test_signal.c, signal_pendingset_main, tc_signal.c, CONFIG_TC_KERNEL_SIGNAL)
#endif
#if defined(CONFIG_TC_KERNEL_WDOG) && defined(CONFIG_WATCHDOG)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_WDOG_TEST, 35, test_wdog, test_wdog.c, wdog_main, tc_wdog.c, CONFIG_TC_KERNEL_WDOG)
#endif
#if defined(CONFIG_TC_KERNEL_LOG_DUMP) && defined(CONFIG_LOG_DUMP)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_LOG_DUMP_TEST, 42, test_log_dump, test_log_dump.c, log_dump_main, tc_log_dump.c, CONFIG_TC_KERNEL_LOG_DUMP)
#endif
#if defined(CONFIG_TC_KERNEL_BINARY_MANAGER) && defined(CONFIG_BINARY_MANAGER)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_BINARY_MANAGER_TEST, 43, test_binary_manager, test_binary_manager.c, binary_manager_main, tc_binary_manager.c, CONFIG_TC_KERNEL_BINARY_MANAGER)
#endif
#if defined(CONFIG_TC_KERNEL_MEM_LEAK_CHECKER) && defined(CONFIG_MEM_LEAK_CHECKER)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_MEM_LEAK_CHECKER_TEST, 44, test_mem_leak_checker, test_mem_leak_checker.c, mem_leak_checker_main, tc_mem_leak_checker.c, CONFIG_TC_KERNEL_MEM_LEAK_CHECKER)
#endif
#if defined(CONFIG_TC_KERNEL_REBOOT_REASON) && defined(CONFIG_SYSTEM_REBOOT_REASON)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_REBOOT_REASON_TEST, 45, test_reboot_reason, test_reboot_reason.c, reboot_reason_main, tc_reboot_reason.c, CONFIG_TC_KERNEL_REBOOT_REASON)
#endif
#if defined(CONFIG_TC_KERNEL_PM) && defined(CONFIG_PM)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_PM_TEST, 46, test_pm, test_pm.c, pm_tc_main, tc_pm.c, CONFIG_TC_KERNEL_PM)
#endif
#if defined(CONFIG_TC_KERNEL_RTC) && defined(CONFIG_RTC_DRIVER)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_RTC_TEST, 48, test_rtc, test_rtc.c, rtc_main, tc_rtc.c, CONFIG_TC_KERNEL_RTC)
#endif
#if defined(CONFIG_TC_KERNEL_BINFMT) && defined(CONFIG_BINFMT_ENABLE)
OS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_BINFMT_TEST, 50, test_binfmt, test_binfmt.c, binfmt_main, tc_binfmt.c, CONFIG_TC_KERNEL_BINFMT)
#endif
