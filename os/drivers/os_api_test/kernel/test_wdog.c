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
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <errno.h>
#include <debug.h>
#include <stdint.h>
#include <unistd.h>

#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>
#include <tinyara/wdog.h>

#include "wdog/wdog.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_WDOG_DELAY_TICKS		2
#define TEST_WDOG_RECOVER_DELAY_TICKS	20
#define TEST_WDOG_LONG_DELAY_TICKS	100
#define TEST_WDOG_WAIT_USEC		10000
#define TEST_WDOG_WAIT_RETRY		50
#define TEST_WDOG_ARG1			0x12345678
#define TEST_WDOG_ARG2			0x87654321
#define TEST_WDOG_ARG3			0x13572468
#define TEST_WDOG_ARG4			0x24681357

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile int g_wdog_callback_count;
static volatile int g_wdog_callback_argc;
static volatile uint32_t g_wdog_callback_args[4];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void test_wdog_callback0(int argc)
{
	g_wdog_callback_argc = argc;
	g_wdog_callback_count++;
}

#if CONFIG_MAX_WDOGPARMS >= 4
static void test_wdog_callback4(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
	g_wdog_callback_argc = argc;
	g_wdog_callback_args[0] = arg1;
	g_wdog_callback_args[1] = arg2;
	g_wdog_callback_args[2] = arg3;
	g_wdog_callback_args[3] = arg4;
	g_wdog_callback_count++;
}
#endif

static void test_wdog_reset_callback_state(void)
{
	int i;

	g_wdog_callback_count = 0;
	g_wdog_callback_argc = -1;
	for (i = 0; i < 4; i++) {
		g_wdog_callback_args[i] = 0;
	}
}

static int test_wdog_wait_callback(void)
{
	int retry;

	for (retry = 0; retry < TEST_WDOG_WAIT_RETRY && g_wdog_callback_count == 0; retry++) {
		usleep(TEST_WDOG_WAIT_USEC);
	}

	return g_wdog_callback_count;
}

static int test_wdog_basic(void)
{
	WDOG_ID wdog;

	set_errno(0);
	if (wd_start(NULL, TEST_WDOG_DELAY_TICKS, (wdentry_t)test_wdog_callback0, 0) != ERROR || get_errno() != EINVAL) {
		dbg("wd_start accepted NULL watchdog.\n");
		return ERROR;
	}

	wdog = wd_create();
	if (wdog == NULL) {
		dbg("wd_create failed.\n");
		return ERROR;
	}

	if (!wd_is_prealloc(wdog) && !WDOG_ISALLOCED(wdog)) {
		dbg("wd_create returned unexpected watchdog storage.\n");
		goto errout_with_wdog;
	}

	if (wd_gettime(wdog) != 0) {
		dbg("inactive watchdog reported remaining time.\n");
		goto errout_with_wdog;
	}

	set_errno(0);
	if (wd_start(wdog, -1, (wdentry_t)test_wdog_callback0, 0) != ERROR || get_errno() != EINVAL) {
		dbg("wd_start accepted negative delay.\n");
		goto errout_with_wdog;
	}

	set_errno(0);
	if (wd_start(wdog, TEST_WDOG_DELAY_TICKS, (wdentry_t)test_wdog_callback0,
				 CONFIG_MAX_WDOGPARMS + 1) != ERROR ||
		get_errno() != EINVAL) {
		dbg("wd_start accepted too many callback arguments.\n");
		goto errout_with_wdog;
	}

#ifdef CONFIG_SCHED_WAKEUPSOURCE
	set_errno(0);
	if (wd_setwakeupsource(NULL) != ERROR || get_errno() != EINVAL) {
		dbg("wd_setwakeupsource accepted NULL watchdog.\n");
		goto errout_with_wdog;
	}

	if (wd_setwakeupsource(wdog) != OK || !WDOG_ISWAKEUP(wdog)) {
		dbg("wd_setwakeupsource failed.\n");
		goto errout_with_wdog;
	}
#endif

	if (wd_start(wdog, TEST_WDOG_LONG_DELAY_TICKS, (wdentry_t)test_wdog_callback0, 0) != OK) {
		dbg("wd_start failed.\n");
		goto errout_with_wdog;
	}

	if (!WDOG_ISACTIVE(wdog) || wd_gettime(wdog) <= 0) {
		dbg("active watchdog state is invalid.\n");
		goto errout_with_active_wdog;
	}

#ifdef CONFIG_SCHED_WAKEUPSOURCE
	set_errno(0);
	if (wd_setwakeupsource(wdog) != ERROR || get_errno() != EINVAL) {
		dbg("wd_setwakeupsource accepted active watchdog.\n");
		goto errout_with_active_wdog;
	}

	if (wd_getwakeupdelay() <= 0) {
		dbg("wd_getwakeupdelay did not report active wakeup watchdog.\n");
		goto errout_with_active_wdog;
	}
#endif

	if (wd_cancel(wdog) != OK) {
		dbg("wd_cancel failed.\n");
		goto errout_with_wdog;
	}

	if (WDOG_ISACTIVE(wdog) || wd_gettime(wdog) != 0) {
		dbg("wd_cancel did not clear active state.\n");
		goto errout_with_wdog;
	}

	if (wd_cancel(wdog) != ERROR) {
		dbg("wd_cancel accepted inactive watchdog.\n");
		goto errout_with_wdog;
	}

#ifdef CONFIG_SCHED_WAKEUPSOURCE
	WDOG_CLRWAKEUP(wdog);
#endif

	if (wd_delete(wdog) != OK) {
		dbg("wd_delete failed.\n");
		return ERROR;
	}

	return OK;

errout_with_active_wdog:
	wd_cancel(wdog);
errout_with_wdog:
#ifdef CONFIG_SCHED_WAKEUPSOURCE
	WDOG_CLRWAKEUP(wdog);
#endif
	wd_delete(wdog);
	return ERROR;
}

static int test_wdog_queue_order(void)
{
	WDOG_ID wdog1;
	WDOG_ID wdog2;
	int time1;
	int time2;

	wdog1 = wd_create();
	if (wdog1 == NULL) {
		dbg("wd_create failed.\n");
		return ERROR;
	}

	wdog2 = wd_create();
	if (wdog2 == NULL) {
		dbg("wd_create failed.\n");
		wd_delete(wdog1);
		return ERROR;
	}

	if (wd_start(wdog1, TEST_WDOG_LONG_DELAY_TICKS, (wdentry_t)test_wdog_callback0, 0) != OK) {
		dbg("wd_start failed.\n");
		goto errout_with_wdogs;
	}

	if (wd_start(wdog2, TEST_WDOG_LONG_DELAY_TICKS * 2, (wdentry_t)test_wdog_callback0, 0) != OK) {
		dbg("wd_start failed.\n");
		goto errout_with_wdogs;
	}

	time1 = wd_gettime(wdog1);
	time2 = wd_gettime(wdog2);
	if (!WDOG_ISACTIVE(wdog1) || !WDOG_ISACTIVE(wdog2) || time1 <= 0 || time2 <= time1) {
		dbg("watchdog queue ordering is invalid.\n");
		goto errout_with_wdogs;
	}

	wd_cancel(wdog1);
	wd_cancel(wdog2);
	wd_delete(wdog1);
	wd_delete(wdog2);
	return OK;

errout_with_wdogs:
	wd_delete(wdog1);
	wd_delete(wdog2);
	return ERROR;
}

static int test_wdog_expiration(void)
{
	WDOG_ID wdog;

	wdog = wd_create();
	if (wdog == NULL) {
		dbg("wd_create failed.\n");
		return ERROR;
	}

	test_wdog_reset_callback_state();

#if CONFIG_MAX_WDOGPARMS >= 4
	if (wd_start(wdog, TEST_WDOG_DELAY_TICKS, (wdentry_t)test_wdog_callback4,
				 4, TEST_WDOG_ARG1, TEST_WDOG_ARG2,
				 TEST_WDOG_ARG3, TEST_WDOG_ARG4) != OK) {
#else
	if (wd_start(wdog, TEST_WDOG_DELAY_TICKS, (wdentry_t)test_wdog_callback0, 0) != OK) {
#endif
		dbg("wd_start failed.\n");
		wd_delete(wdog);
		return ERROR;
	}

	if (test_wdog_wait_callback() != 1) {
		dbg("watchdog callback did not run.\n");
		goto errout_with_wdog;
	}

#if CONFIG_MAX_WDOGPARMS >= 4
	if (g_wdog_callback_argc != 4 ||
		g_wdog_callback_args[0] != TEST_WDOG_ARG1 ||
		g_wdog_callback_args[1] != TEST_WDOG_ARG2 ||
		g_wdog_callback_args[2] != TEST_WDOG_ARG3 ||
		g_wdog_callback_args[3] != TEST_WDOG_ARG4) {
		dbg("watchdog callback arguments are invalid.\n");
		goto errout_with_wdog;
	}
#else
	if (g_wdog_callback_argc != 0) {
		dbg("watchdog callback argc is invalid.\n");
		goto errout_with_wdog;
	}
#endif

	if (WDOG_ISACTIVE(wdog) || wd_gettime(wdog) != 0) {
		dbg("expired watchdog is still active.\n");
		goto errout_with_wdog;
	}

	if (wd_delete(wdog) != OK) {
		dbg("wd_delete failed.\n");
		return ERROR;
	}

	return OK;

errout_with_wdog:
	wd_delete(wdog);
	return ERROR;
}

static int test_wdog_recover(void)
{
	FAR struct tcb_s *self;
	WDOG_ID wdog;

	wdog = wd_create();
	if (wdog == NULL) {
		dbg("wd_create failed.\n");
		return ERROR;
	}

	test_wdog_reset_callback_state();

	if (wd_start(wdog, TEST_WDOG_RECOVER_DELAY_TICKS, (wdentry_t)test_wdog_callback0, 0) != OK) {
		dbg("wd_start failed.\n");
		wd_delete(wdog);
		return ERROR;
	}

	self = sched_self();
	if (self == NULL) {
		dbg("sched_self failed.\n");
		wd_delete(wdog);
		return ERROR;
	}

	if (self->waitdog != NULL) {
		dbg("current task already has waitdog.\n");
		wd_delete(wdog);
		return ERROR;
	}

	self->waitdog = wdog;
	wd_recover(self);
	if (self->waitdog != NULL) {
		dbg("wd_recover did not clear waitdog.\n");
		wd_delete(self->waitdog);
		self->waitdog = NULL;
		return ERROR;
	}

	usleep(TEST_WDOG_WAIT_USEC * TEST_WDOG_WAIT_RETRY);
	if (g_wdog_callback_count != 0) {
		dbg("recovered watchdog callback still ran.\n");
		return ERROR;
	}

	return OK;
}

static int test_wdog_static(void)
{
	struct wdog_s wdog;

	wd_static(&wdog);
	if (!WDOG_ISSTATIC(&wdog) || WDOG_ISACTIVE(&wdog) || WDOG_ISALLOCED(&wdog)) {
		dbg("wd_static initialized unexpected flags.\n");
		return ERROR;
	}

	if (wd_start(&wdog, TEST_WDOG_LONG_DELAY_TICKS, (wdentry_t)test_wdog_callback0, 0) != OK) {
		dbg("wd_start failed for static watchdog.\n");
		return ERROR;
	}

	if (!WDOG_ISACTIVE(&wdog) || wd_gettime(&wdog) <= 0) {
		dbg("static watchdog active state is invalid.\n");
		goto errout_with_wdog;
	}

	if (wd_cancel(&wdog) != OK) {
		dbg("wd_cancel failed for static watchdog.\n");
		goto errout_with_wdog;
	}

	if (WDOG_ISACTIVE(&wdog) || wd_gettime(&wdog) != 0) {
		dbg("static watchdog cancel did not clear active state.\n");
		goto errout_with_wdog;
	}

	if (wd_delete(&wdog) != OK || !WDOG_ISSTATIC(&wdog)) {
		dbg("wd_delete failed for static watchdog.\n");
		return ERROR;
	}

	return OK;

errout_with_wdog:
	wd_cancel(&wdog);
	return ERROR;
}

static int test_wdog_all(unsigned long arg)
{
	(void)arg;

	if (test_wdog_basic() != OK) {
		return ERROR;
	}

	if (test_wdog_queue_order() != OK) {
		return ERROR;
	}

	if (test_wdog_expiration() != OK) {
		return ERROR;
	}

	if (test_wdog_recover() != OK) {
		return ERROR;
	}

	if (test_wdog_static() != OK) {
		return ERROR;
	}

	return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_wdog(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_WDOG_TEST:
		ret = test_wdog_all(arg);
		break;
	}

	return ret;
}
