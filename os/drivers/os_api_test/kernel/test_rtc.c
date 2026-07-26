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
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <tinyara/os_api_test_drv.h>
#include <tinyara/rtc.h>

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_RTC_DEVPATH	"/dev/rtc0"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_rtc_validate_time(FAR const struct rtc_time *rtctime)
{
	if (rtctime->tm_sec < 0 || rtctime->tm_sec > 61 ||
		rtctime->tm_min < 0 || rtctime->tm_min > 59 ||
		rtctime->tm_hour < 0 || rtctime->tm_hour > 23 ||
		rtctime->tm_mday < 1 || rtctime->tm_mday > 31 ||
		rtctime->tm_mon < 0 || rtctime->tm_mon > 11 ||
		rtctime->tm_year < 0) {
		dbg("rtc returned invalid time fields.\n");
		return ERROR;
	}

	return OK;
}

static int test_rtc_open_close(void)
{
	int fd;

	fd = open(TEST_RTC_DEVPATH, O_RDWR);
	if (fd < 0) {
		dbg("failed to open %s: %d.\n", TEST_RTC_DEVPATH, get_errno());
		return ERROR;
	}

	if (close(fd) != OK) {
		dbg("failed to close %s.\n", TEST_RTC_DEVPATH);
		return ERROR;
	}

	return OK;
}

static int test_rtc_read_write(void)
{
	char byte = 0;
	int fd;
	int ret;

	fd = open(TEST_RTC_DEVPATH, O_RDWR);
	if (fd < 0) {
		dbg("failed to open %s: %d.\n", TEST_RTC_DEVPATH, get_errno());
		return ERROR;
	}

	ret = read(fd, &byte, sizeof(byte));
	if (ret != 0) {
		dbg("rtc read returned unexpected result: %d.\n", ret);
		close(fd);
		return ERROR;
	}

	ret = write(fd, &byte, 0);
	if (ret != 0) {
		dbg("rtc zero-length write returned unexpected result: %d.\n", ret);
		close(fd);
		return ERROR;
	}

	ret = write(fd, &byte, sizeof(byte));
	if (ret != (int)sizeof(byte)) {
		dbg("rtc write returned unexpected result: %d.\n", ret);
		close(fd);
		return ERROR;
	}

	if (close(fd) != OK) {
		dbg("failed to close %s.\n", TEST_RTC_DEVPATH);
		return ERROR;
	}

	return OK;
}

static int test_rtc_ioctl(void)
{
	struct rtc_time rtctime;
	int fd;
	int ret;

	fd = open(TEST_RTC_DEVPATH, O_RDWR);
	if (fd < 0) {
		dbg("failed to open %s: %d.\n", TEST_RTC_DEVPATH, get_errno());
		return ERROR;
	}

	ret = ioctl(fd, RTC_RD_TIME, (unsigned long)((uintptr_t)&rtctime));
	if (ret < 0) {
		dbg("RTC_RD_TIME failed: %d.\n", ret);
		close(fd);
		return ERROR;
	}

	if (test_rtc_validate_time(&rtctime) != OK) {
		close(fd);
		return ERROR;
	}

	ret = ioctl(fd, RTC_SET_TIME, (unsigned long)((uintptr_t)&rtctime));
	if (ret < 0) {
		dbg("RTC_SET_TIME failed: %d.\n", ret);
		close(fd);
		return ERROR;
	}

#ifndef CONFIG_RTC_ALARM
	set_errno(0);
	ret = ioctl(fd, RTC_SET_ALARM, 0);
	if (ret != ERROR || get_errno() != ENOTTY) {
		dbg("rtc accepted unsupported alarm ioctl: %d.\n", ret);
		close(fd);
		return ERROR;
	}

	set_errno(0);
	ret = ioctl(fd, RTC_SET_RELATIVE, 0);
	if (ret != ERROR || get_errno() != ENOTTY) {
		dbg("rtc accepted unsupported relative alarm ioctl: %d.\n", ret);
		close(fd);
		return ERROR;
	}

	set_errno(0);
	ret = ioctl(fd, RTC_CANCEL_ALARM, 0);
	if (ret != ERROR || get_errno() != ENOTTY) {
		dbg("rtc accepted unsupported cancel alarm ioctl: %d.\n", ret);
		close(fd);
		return ERROR;
	}
#endif

	set_errno(0);
	ret = ioctl(fd, -1, 0);
	if (ret != ERROR || get_errno() != ENOTTY) {
		dbg("rtc returned unexpected invalid ioctl result: %d.\n", ret);
		close(fd);
		return ERROR;
	}

	if (close(fd) != OK) {
		dbg("failed to close %s.\n", TEST_RTC_DEVPATH);
		return ERROR;
	}

	return OK;
}

static int test_rtc_core(unsigned long arg)
{
	(void)arg;

	if (test_rtc_open_close() != OK) {
		return ERROR;
	}

	if (test_rtc_read_write() != OK) {
		return ERROR;
	}

	return test_rtc_ioctl();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_rtc(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_RTC_TEST:
		ret = test_rtc_core(arg);
		break;
	}

	return ret;
}
