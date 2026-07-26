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

#define OS_API_TEST_KERNEL_DESCRIPTOR(symbol, id, provider, provider_source, wrapper, wrapper_source, test_gate) \
	enum { symbol = _TESTIOC(id) };
#include "../../drivers/os_api_test/os_api_test_kernel_registry.inc"
#undef OS_API_TEST_KERNEL_DESCRIPTOR

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
