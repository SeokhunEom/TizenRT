/****************************************************************************
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
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

#ifndef __INCLUDE_TINYARA_OS_API_TEST_DRV_H
#define __INCLUDE_TINYARA_OS_API_TEST_DRV_H

/* This file will be used to provide definitions to support
 * OS API test case framework
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <tinyara/fs/ioctl.h>

#if defined(CONFIG_TC_NET_PBUF)
#include <lwip/pbuf.h>
#endif

#ifdef CONFIG_DRIVERS_OS_API_TEST

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
/* Configuration
 *
 * CONFIG_DRIVERS_OS_API_TEST - Enables OS API test driver support
 */

/* IOCTL Commands */
/* The OS API test module uses ioctl commands to identify which
 * test cases are to be run. The ioctl command may be accompanied by and arguement to
 * indicate which particular API  in the module is to be tested or which particular
 * test scenario is to be run
 *
 * TESTIOC_TEST_DRIVER_ANALOG - Run test cases for os/drivers/analog module
 *
 *   ioctl argument:  Integer (enum or DEFINE value) indicating the particular test case that is to be run
 *
 */

#define TESTIOC_ANALOG                         _TESTIOC(1)
#define TESTIOC_GET_SIG_FINDACTION_ADD         _TESTIOC(2)
#define TESTIOC_GET_SELF_PID                   _TESTIOC(3)
#define TESTIOC_IS_ALIVE_THREAD                _TESTIOC(4)
#define TESTIOC_GET_TCB_SIGPROCMASK            _TESTIOC(5)
#define TESTIOC_GET_TCB_ADJ_STACK_SIZE         _TESTIOC(6)
#define TESTIOC_SCHED_FOREACH                  _TESTIOC(8)
#define TESTIOC_SIGNAL_PAUSE                   _TESTIOC(9)
#define TESTIOC_CLOCK_ABSTIME2TICKS_TEST       _TESTIOC(10)
#define TESTIOC_TIMER_INITIALIZE_TEST          _TESTIOC(11)
#define TESTIOC_SEM_TICK_WAIT_TEST             _TESTIOC(12)
#define TESTIOC_TASK_REPARENT                  _TESTIOC(13)
#if defined(CONFIG_SCHED_HAVE_PARENT) && defined(CONFIG_SCHED_CHILD_STATUS)
#define TESTIOC_GROUP_ADD_FINED_REMOVE_TEST    _TESTIOC(14)
#define TESTIOC_GROUP_ALLOC_FREE_TEST          _TESTIOC(15)
#define TESTIOC_GROUP_EXIT_CHILD_TEST          _TESTIOC(16)
#define TESTIOC_GROUP_REMOVECHILDREN_TEST      _TESTIOC(17)
#endif
#define TESTIOC_TASK_INIT_TEST                 _TESTIOC(18)
#define TESTIOC_COMPRESSION_TEST	        _TESTIOC(19)
#ifdef CONFIG_EXAMPLES_MEM_PROTECT_TEST
#define TESTIOC_MEM_PROTECTTEST			_TESTIOC(20)
#endif
#ifdef CONFIG_ARMV8M_TRUSTZONE
#define TESTIOC_TZ				_TESTIOC(21)
#endif
#ifdef CONFIG_EXAMPLES_STACK_PROTECTION
#define TESTIOC_KTHREAD_STACK_PROTECTION_TEST	_TESTIOC(22)
#endif
#if defined(CONFIG_TC_NET_PBUF) || defined(CONFIG_TC_KERNEL_NET_PBUF)
#define TESTIOC_NET_PBUF			_TESTIOC(23)
#endif
#if defined(CONFIG_AUTOMOUNT_USERFS) && defined(CONFIG_EXAMPLES_TESTCASE_FILESYSTEM)
#define TESTIOC_GET_FS_PARTNO			_TESTIOC(24)
#endif
#define TESTIOC_TIMER_CREATE_DELETE_TEST	_TESTIOC(25)
#define TESTIOC_TASK_SETCANCELSTATE_TEST	_TESTIOC(26)
#ifdef CONFIG_CANCELLATION_POINTS
#define TESTIOC_TASK_SETCANCELTYPE_TEST		_TESTIOC(27)
#endif
#define TESTIOC_WORK_QUEUE_TEST			_TESTIOC(28)
#define TESTIOC_KMM_HEAP_TEST			_TESTIOC(29)
#define TESTIOC_TASK_LIFECYCLE_TEST		_TESTIOC(30)
#define TESTIOC_SCHED_FOREACH_TEST		_TESTIOC(31)
#define TESTIOC_SIG_FINDACTION_NULL_TEST	_TESTIOC(32)
#define TESTIOC_CLOCK_CONVERSION_TEST		_TESTIOC(33)
#define TESTIOC_TIMER_DELETEALL_TEST		_TESTIOC(34)
#define TESTIOC_WDOG_TEST			_TESTIOC(35)
#define TESTIOC_MQUEUE_TEST			_TESTIOC(36)
#define TESTIOC_SEM_KERNEL_TEST			_TESTIOC(37)
#define TESTIOC_ENVIRON_TEST			_TESTIOC(38)
#define TESTIOC_ERRNO_TEST			_TESTIOC(39)
#ifndef CONFIG_DISABLE_PTHREAD
#define TESTIOC_PTHREAD_TEST			_TESTIOC(40)
#endif
#define TESTIOC_IRQ_TEST			_TESTIOC(41)
#define TESTIOC_LOG_DUMP_TEST			_TESTIOC(42)
#define TESTIOC_BINARY_MANAGER_TEST		_TESTIOC(43)
#define TESTIOC_MEM_LEAK_CHECKER_TEST		_TESTIOC(44)
#define TESTIOC_REBOOT_REASON_TEST		_TESTIOC(45)
#define TESTIOC_PM_TEST				_TESTIOC(46)
#define TESTIOC_PROCFS_TEST			_TESTIOC(47)
#define TESTIOC_RTC_TEST			_TESTIOC(48)
#define TESTIOC_PIPE_TEST			_TESTIOC(49)
#define TESTIOC_BINFMT_TEST			_TESTIOC(50)
#define TESTIOC_VFS_TEST			_TESTIOC(51)
#define TESTIOC_TERMIOS_TEST			_TESTIOC(52)
#define TESTIOC_SCHED_AFFINITY_TEST		_TESTIOC(53)
#if defined(CONFIG_SCHED_HAVE_PARENT) && defined(CONFIG_SCHED_CHILD_STATUS) && !defined(CONFIG_DISABLE_SIGNALS)
#define TESTIOC_GROUP_SIGNAL_TEST		_TESTIOC(54)
#endif
#ifdef CONFIG_SCHED_STARTHOOK
#define TESTIOC_TASK_STARTHOOK_TEST		_TESTIOC(55)
#endif
#define TESTIOC_SCHED_STATE_TEST		_TESTIOC(56)
#define TESTIOC_SIG_PENDINGSET_TEST		_TESTIOC(57)

#define OS_API_TEST_DRVPATH	"/dev/os_api_test"

/****************************************************************************
 * Public Types
 ****************************************************************************/

#if defined(CONFIG_TC_NET_PBUF)
struct pbuf_test_args {
	pbuf_layer layer;
	u16_t len;
	pbuf_type type;
};
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: os_api_test_drv_register
 *
 * Description:
 *   This function creates a device node like "/dev/os_api_test" which will be used
 *   by the tests that execute OS(kernel, network and fs) side APIs
 *
 *
 ****************************************************************************/

void os_api_test_drv_register(void);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif							/* CONFIG_DRIVERS_OS_API_TEST */
#endif							/* __INCLUDE_TINYARA_OS_API_TEST_DRV_H */
