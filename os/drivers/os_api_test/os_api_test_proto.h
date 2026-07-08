/****************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
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

#ifndef __DRIVERS_OS_API_TEST_KERNEL_TEST_PROTO_H
#define __DRIVERS_OS_API_TEST_KERNEL_TEST_PROTO_H
int test_task(int cmd, unsigned long arg);
int test_sem(int cmd, unsigned long arg);
int test_group(int cmd, unsigned long arg);
int test_clock(int cmd, unsigned long arg);
#ifndef CONFIG_DISABLE_POSIX_TIMERS
int test_timer(int cmd, unsigned long arg);
#endif
#ifdef CONFIG_WATCHDOG
int test_wdog(int cmd, unsigned long arg);
#endif
int test_sched(int cmd, unsigned long arg);
int test_wqueue(int cmd, unsigned long arg);
int test_kmm(int cmd, unsigned long arg);
#ifndef CONFIG_DISABLE_ENVIRON
int test_environ(int cmd, unsigned long arg);
#endif
int test_errno(int cmd, unsigned long arg);
#ifndef CONFIG_DISABLE_PTHREAD
int test_pthread(int cmd, unsigned long arg);
#endif
int test_irq(int cmd, unsigned long arg);
#ifdef CONFIG_BINFMT_ENABLE
int test_binfmt(int cmd, unsigned long arg);
#endif
#ifdef CONFIG_LOG_DUMP
int test_log_dump(int cmd, unsigned long arg);
#endif
#ifdef CONFIG_BINARY_MANAGER
int test_binary_manager(int cmd, unsigned long arg);
#endif
#ifdef CONFIG_MEM_LEAK_CHECKER
int test_mem_leak_checker(int cmd, unsigned long arg);
#endif
#ifdef CONFIG_SYSTEM_REBOOT_REASON
int test_reboot_reason(int cmd, unsigned long arg);
#endif
#ifdef CONFIG_PM
int test_pm(int cmd, unsigned long arg);
#endif
#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_UPTIME) && !defined(CONFIG_FS_PROCFS_EXCLUDE_VERSION)
int test_procfs(int cmd, unsigned long arg);
#endif
#ifdef CONFIG_RTC_DRIVER
int test_rtc(int cmd, unsigned long arg);
#endif
#ifdef CONFIG_PIPES
int test_pipe(int cmd, unsigned long arg);
#endif
int test_vfs(int cmd, unsigned long arg);
#ifdef CONFIG_SERIAL_TERMIOS
int test_termios(int cmd, unsigned long arg);
#endif
#ifndef CONFIG_DISABLE_MQUEUE
int test_mqueue(int cmd, unsigned long arg);
#endif
#ifndef CONFIG_DISABLE_SIGNALS
int test_signal(int cmd, unsigned long arg);
#endif
int test_compress_decompress(int cmd, unsigned long arg);
#ifdef CONFIG_ARMV8M_TRUSTZONE
int test_tz(void);
#endif
#ifdef CONFIG_EXAMPLES_STACK_PROTECTION
int test_kthread_stack_overflow_protection(int cmd, unsigned long arg);
#endif
#if defined(CONFIG_TC_NET_PBUF) || defined(CONFIG_TC_KERNEL_NET_PBUF)
int test_net_pbuf(int cmd, unsigned long arg);
#endif
#if defined(CONFIG_AUTOMOUNT_USERFS) && defined(CONFIG_EXAMPLES_TESTCASE_FILESYSTEM)
int test_fs_get_devname(void);
#endif
#endif
