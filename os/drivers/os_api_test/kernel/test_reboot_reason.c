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

#include <arch/reboot_reason.h>
#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/prctl.h>
#include <sys/types.h>

#include <tinyara/os_api_test_drv.h>
#include <tinyara/reboot_reason.h>

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_REBOOT_REASON_VALUE	REBOOT_SYSTEM_USER_INTENDED

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_reboot_reason_validate_code(reboot_reason_code_t reason)
{
	if (reason < REBOOT_REASON_INITIALIZED || reason > REBOOT_UNKNOWN) {
		dbg("invalid reboot reason: %d.\n", reason);
		return ERROR;
	}

	return OK;
}

static int test_reboot_reason_read(unsigned long arg)
{
	reboot_reason_code_t reason;

	(void)arg;

	reason = up_reboot_reason_read();
	if (test_reboot_reason_validate_code(reason) != OK) {
		return ERROR;
	}

	if (prctl(PR_REBOOT_REASON_READ) != reason) {
		dbg("prctl reboot reason read mismatch.\n");
		return ERROR;
	}

	(void)up_reboot_reason_is_written();

	return OK;
}

static int test_reboot_reason_state_transition(unsigned long arg)
{
	reboot_reason_code_t original_reason;
	bool original_written;
	int ret = ERROR;

	(void)arg;

	original_reason = up_reboot_reason_read();
	original_written = up_reboot_reason_is_written();

	if (original_written || original_reason != REBOOT_REASON_INITIALIZED) {
		return OK;
	}

	if (prctl(PR_REBOOT_REASON_WRITE, TEST_REBOOT_REASON_VALUE) != OK) {
		dbg("failed to write reboot reason through prctl.\n");
		return ERROR;
	}

	if (!up_reboot_reason_is_written()) {
		dbg("reboot reason write was not observed.\n");
		goto errout_restore;
	}

	if (prctl(PR_REBOOT_REASON_CLEAR) != OK) {
		dbg("failed to clear reboot reason through prctl.\n");
		goto errout_restore;
	}

	if (up_reboot_reason_is_written()) {
		dbg("reboot reason clear was not observed.\n");
		goto errout_restore;
	}

	if (up_reboot_reason_read() != REBOOT_REASON_INITIALIZED) {
		dbg("reboot reason clear did not reset cached reason.\n");
		goto errout_restore;
	}

	ret = OK;

errout_restore:
	up_reboot_reason_clear();

	return ret;
}

static int test_reboot_reason_core(unsigned long arg)
{
	if (test_reboot_reason_read(arg) != OK) {
		return ERROR;
	}

	return test_reboot_reason_state_transition(arg);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_reboot_reason(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_REBOOT_REASON_TEST:
		ret = test_reboot_reason_core(arg);
		break;
	}

	return ret;
}
