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
#include <string.h>

#include <tinyara/log_dump/log_dump.h>
#include <tinyara/log_dump/log_dump_internal.h>
#include <tinyara/os_api_test_drv.h>

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_LOG_DUMP_INVALID_OPT	"invalid"
#define TEST_LOG_DUMP_FAIL		(-99)
#define TEST_LOG_DUMP_OPT_FAIL		(-2)
#define TEST_LOG_DUMP_MARKER		'T'

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_log_dump_control(unsigned long arg)
{
	int ret;
	int was_started;
	int result = ERROR;

	(void)arg;

	was_started = (log_dump_get_size() == TEST_LOG_DUMP_FAIL);

	ret = log_dump_set(LOGDUMP_SAVE_START, strlen(LOGDUMP_SAVE_START) + 1);
	if (ret != OK) {
		dbg("log_dump_set start failed: %d\n", ret);
		return ERROR;
	}

	ret = log_dump_set(TEST_LOG_DUMP_INVALID_OPT, sizeof(TEST_LOG_DUMP_INVALID_OPT));
	if (ret != TEST_LOG_DUMP_OPT_FAIL) {
		dbg("log_dump_set accepted invalid option: %d\n", ret);
		goto out;
	}

	ret = log_dump_get_size();
	if (ret != TEST_LOG_DUMP_FAIL) {
		dbg("log_dump_get_size did not fail while saving: %d\n", ret);
		goto out;
	}

	ret = log_dump_set(LOGDUMP_GET_SIZE, strlen(LOGDUMP_GET_SIZE) + 1);
	if (ret != TEST_LOG_DUMP_FAIL) {
		dbg("log_dump_set get-size did not report active-save failure: %d\n", ret);
		goto out;
	}

	ret = log_dump_save(TEST_LOG_DUMP_MARKER);
	if (ret < 0) {
		dbg("log_dump_save failed: %d\n", ret);
		goto out;
	}

	ret = log_dump_set(LOGDUMP_SAVE_STOP, strlen(LOGDUMP_SAVE_STOP) + 1);
	if (ret != OK) {
		dbg("log_dump_set stop failed: %d\n", ret);
		goto out;
	}

	ret = log_dump_get_size();
	if (ret < 0) {
		dbg("log_dump_get_size failed after stopping: %d\n", ret);
		goto out;
	}

	ret = log_dump_set(LOGDUMP_GET_SIZE, strlen(LOGDUMP_GET_SIZE) + 1);
	if (ret < 0) {
		dbg("log_dump_set get-size failed after stopping: %d\n", ret);
		goto out;
	}

	result = OK;

out:
	if (was_started) {
		ret = log_dump_set(LOGDUMP_SAVE_START, strlen(LOGDUMP_SAVE_START) + 1);
	} else {
		ret = log_dump_set(LOGDUMP_SAVE_STOP, strlen(LOGDUMP_SAVE_STOP) + 1);
	}

	if (ret != OK) {
		dbg("log_dump_set restore failed: %d\n", ret);
		return ERROR;
	}

	return result;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_log_dump(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_LOG_DUMP_TEST:
		ret = test_log_dump_control(arg);
		break;
	}

	return ret;
}
