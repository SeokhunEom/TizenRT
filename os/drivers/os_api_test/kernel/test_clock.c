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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <sys/types.h>
#include <debug.h>
#include <errno.h>
#include <time.h>

#include <tinyara/os_api_test_drv.h>

#include "clock/clock.h"

/****************************************************************************
 * Private Function
 ****************************************************************************/

static int test_clock_abstime2ticks(unsigned long arg)
{
	int ret_chk;
	int base_tick;
	int comparison_tick;
	struct timespec cur_time;
	struct timespec base_time;
	struct timespec comparison_time;
	struct timespec result_time;

	(void)arg;

	ret_chk = clock_gettime(CLOCK_REALTIME, &cur_time);
	if (ret_chk != OK) {
		dbg("clock_gettime failed. errno : %d\n", get_errno());
		return ERROR;
	}

	base_time.tv_sec = cur_time.tv_sec + 101;
	base_time.tv_nsec = cur_time.tv_nsec;

	comparison_time.tv_sec = cur_time.tv_sec + 102;
	comparison_time.tv_nsec = cur_time.tv_nsec;
	ret_chk = clock_abstime2ticks(CLOCK_REALTIME, &base_time, &base_tick);
	if (ret_chk != OK) {
		dbg("clock_abstime2ticks failed. ret : %d\n", ret_chk);
		return ERROR;
	}

	ret_chk = clock_abstime2ticks(CLOCK_REALTIME, &comparison_time, &comparison_tick);
	if (ret_chk != OK) {
		dbg("clock_abstime2ticks failed. ret : %d\n", ret_chk);
		return ERROR;
	}

	clock_ticks2time(comparison_tick - base_tick, &result_time);
	if (result_time.tv_sec != 1) {
		dbg("clock_abstime2ticks failed. %ld.%ld sec is not 1 sec.\n",
			(long)result_time.tv_sec, (long)result_time.tv_nsec);
		return ERROR;
	}
	return OK;
}

static int test_clock_conversion(unsigned long arg)
{
	int ret_chk;
	int ticks;
	struct timespec rel_time;
	struct timespec result_time;
	struct timespec cur_time;
	struct timespec past_time;

	(void)arg;

	ret_chk = clock_getres(CLOCK_REALTIME, &result_time);
	if (ret_chk != OK || result_time.tv_sec != 0 || result_time.tv_nsec != NSEC_PER_TICK) {
		dbg("clock_getres failed. ret : %d %ld.%ld\n", ret_chk, (long)result_time.tv_sec, (long)result_time.tv_nsec);
		return ERROR;
	}

	set_errno(0);
	ret_chk = clock_getres(CLOCK_REALTIME, NULL);
	if (ret_chk != ERROR || get_errno() != EINVAL) {
		dbg("clock_getres accepted NULL result. ret : %d errno : %d\n", ret_chk, get_errno());
		return ERROR;
	}

	set_errno(0);
	ret_chk = clock_getres((clockid_t)-1, &result_time);
	if (ret_chk != ERROR || get_errno() != EINVAL) {
		dbg("clock_getres accepted invalid clock. ret : %d errno : %d\n", ret_chk, get_errno());
		return ERROR;
	}

	set_errno(0);
	ret_chk = clock_gettime(CLOCK_REALTIME, NULL);
	if (ret_chk != ERROR || get_errno() != EINVAL) {
		dbg("clock_gettime accepted NULL result. ret : %d errno : %d\n", ret_chk, get_errno());
		return ERROR;
	}

	set_errno(0);
	ret_chk = clock_gettime((clockid_t)-1, &result_time);
	if (ret_chk != ERROR || get_errno() != EINVAL) {
		dbg("clock_gettime accepted invalid clock. ret : %d errno : %d\n", ret_chk, get_errno());
		return ERROR;
	}

	rel_time.tv_sec = 0;
	rel_time.tv_nsec = 0;
	ret_chk = clock_time2ticks(&rel_time, &ticks);
	if (ret_chk != OK || ticks != 0) {
		dbg("clock_time2ticks zero failed. ret : %d ticks : %d\n", ret_chk, ticks);
		return ERROR;
	}

	ret_chk = clock_ticks2time(0, &result_time);
	if (ret_chk != OK || result_time.tv_sec != 0 || result_time.tv_nsec != 0) {
		dbg("clock_ticks2time zero failed. ret : %d %ld.%ld\n", ret_chk, (long)result_time.tv_sec, (long)result_time.tv_nsec);
		return ERROR;
	}

	rel_time.tv_sec = 1;
	rel_time.tv_nsec = 0;
	ret_chk = clock_time2ticks(&rel_time, &ticks);
	if (ret_chk != OK || ticks != TICK_PER_SEC) {
		dbg("clock_time2ticks failed. ret : %d ticks : %d\n", ret_chk, ticks);
		return ERROR;
	}

	rel_time.tv_sec = 0;
	rel_time.tv_nsec = NSEC_PER_TICK + 1;
	ret_chk = clock_time2ticks(&rel_time, &ticks);
	if (ret_chk != OK || ticks != 2) {
		dbg("clock_time2ticks round-up failed. ret : %d ticks : %d\n", ret_chk, ticks);
		return ERROR;
	}

	ret_chk = clock_ticks2time(TICK_PER_SEC + 2, &result_time);
	if (ret_chk != OK || result_time.tv_sec != 1 || result_time.tv_nsec != 2 * NSEC_PER_TICK) {
		dbg("clock_ticks2time failed. ret : %d %ld.%ld\n", ret_chk, (long)result_time.tv_sec, (long)result_time.tv_nsec);
		return ERROR;
	}

	ret_chk = clock_abstime2ticks((clockid_t)-1, &rel_time, &ticks);
	if (ret_chk != EINVAL) {
		dbg("clock_abstime2ticks invalid clock failed. ret : %d\n", ret_chk);
		return ERROR;
	}

	ret_chk = clock_gettime(CLOCK_REALTIME, &cur_time);
	if (ret_chk != OK) {
		dbg("clock_gettime failed. errno : %d\n", get_errno());
		return ERROR;
	}

	past_time.tv_sec = cur_time.tv_sec - 1;
	past_time.tv_nsec = cur_time.tv_nsec;
	ticks = 0;
	ret_chk = clock_abstime2ticks(CLOCK_REALTIME, &past_time, &ticks);
	if (ret_chk != OK || ticks != -1) {
		dbg("clock_abstime2ticks past time failed. ret : %d ticks : %d\n", ret_chk, ticks);
		return ERROR;
	}

	return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_clock(int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	switch (cmd) {
	case TESTIOC_CLOCK_ABSTIME2TICKS_TEST:
		ret = test_clock_abstime2ticks(arg);
		break;
	case TESTIOC_CLOCK_CONVERSION_TEST:
		ret = test_clock_conversion(arg);
		break;
	}
	return ret;
}
