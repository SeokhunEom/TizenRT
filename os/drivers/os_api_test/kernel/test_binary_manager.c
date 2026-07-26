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

#include <debug.h>
#include <errno.h>
#include <stdint.h>

#include <tinyara/binary_manager.h>
#include <tinyara/os_api_test_drv.h>

#include "binary_manager/binary_manager_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_binary_manager_kernel_metadata(FAR binmgr_kinfo_t *kdata, uint32_t kcount)
{
	uint32_t part_idx;

	if (kcount == 0 || kcount > KERNEL_BIN_COUNT) {
		dbg("invalid kernel partition count: %u.\n", kcount);
		return ERROR;
	}

	if (kdata->inuse_idx >= kcount) {
		dbg("invalid kernel in-use index: %u/%u.\n", (unsigned int)kdata->inuse_idx, kcount);
		return ERROR;
	}

	for (part_idx = 0; part_idx < kcount; part_idx++) {
		if (kdata->part_info[part_idx].size == 0 || kdata->part_info[part_idx].address == 0) {
			dbg("invalid kernel partition metadata: %u.\n", part_idx);
			return ERROR;
		}
	}

	if (binary_manager_verify_kbin((uint8_t)kcount) != BINMGR_INVALID_PARAM) {
		dbg("binary_manager_verify_kbin accepted count as partition index: %u.\n", kcount);
		return ERROR;
	}

	if (binary_manager_get_kbin_version((uint8_t)kcount) != 0) {
		dbg("binary_manager_get_kbin_version returned version for count index: %u.\n", kcount);
		return ERROR;
	}

	return OK;
}

#ifdef CONFIG_APP_BINARY_SEPARATION
static int test_binary_manager_user_metadata(void)
{
	FAR binmgr_uinfo_t *udata;
	uint32_t bin_idx;
	uint32_t part_idx;
	uint32_t ucount;

	ucount = binary_manager_get_ucount();
	if (ucount > USER_BIN_COUNT) {
		dbg("invalid user binary count: %u.\n", ucount);
		return ERROR;
	}

	for (bin_idx = 0; bin_idx <= ucount; bin_idx++) {
		udata = binary_manager_get_udata(bin_idx);
		if (udata == NULL) {
			dbg("binary_manager_get_udata returned NULL for index %u.\n", bin_idx);
			return ERROR;
		}

		if (BIN_COUNT(bin_idx) > PARTS_PER_BIN) {
			dbg("invalid user partition count: %u/%u.\n", bin_idx, (unsigned int)BIN_COUNT(bin_idx));
			return ERROR;
		}

		if (binary_manager_verify_ubin((int)bin_idx, (uint8_t)BIN_COUNT(bin_idx)) != BINMGR_INVALID_PARAM) {
			dbg("binary_manager_verify_ubin accepted count as partition index: %u/%u.\n",
				bin_idx, (unsigned int)BIN_COUNT(bin_idx));
			return ERROR;
		}

		if (binary_manager_get_ubin_version((int)bin_idx, (uint8_t)BIN_COUNT(bin_idx)) != 0) {
			dbg("binary_manager_get_ubin_version returned version for count index: %u/%u.\n",
				bin_idx, (unsigned int)BIN_COUNT(bin_idx));
			return ERROR;
		}

		if (BIN_COUNT(bin_idx) == 0) {
			if (bin_idx == 0) {
				continue;
			}

			dbg("registered user binary has no partition: %u.\n", bin_idx);
			return ERROR;
		}

		if (BIN_NAME(bin_idx)[0] == '\0') {
			dbg("registered user binary has empty name: %u.\n", bin_idx);
			return ERROR;
		}

		if (binary_manager_get_index_with_name(BIN_NAME(bin_idx)) != (int)bin_idx) {
			dbg("failed to look up user binary name: %s.\n", BIN_NAME(bin_idx));
			return ERROR;
		}

		if (BIN_USEIDX(bin_idx) >= BIN_COUNT(bin_idx)) {
			dbg("invalid user in-use index: %u/%u.\n",
				(unsigned int)BIN_USEIDX(bin_idx), (unsigned int)BIN_COUNT(bin_idx));
			return ERROR;
		}

		for (part_idx = 0; part_idx < BIN_COUNT(bin_idx); part_idx++) {
			if (BIN_PARTSIZE(bin_idx, part_idx) == 0 || BIN_PARTADDR(bin_idx, part_idx) == 0) {
				dbg("invalid user partition metadata: %u/%u.\n", bin_idx, part_idx);
				return ERROR;
			}
		}
	}

	return OK;
}
#endif

static int test_binary_manager_core(unsigned long arg)
{
	binmgr_kinfo_t *kdata;
	uint32_t kcount;
#ifdef CONFIG_APP_BINARY_SEPARATION
	uint32_t ucount;
#endif

	(void)arg;

	kdata = binary_manager_get_kdata();
	if (kdata == NULL) {
		dbg("binary_manager_get_kdata returned NULL.\n");
		return ERROR;
	}

	kcount = binary_manager_get_kcount();
	if (kcount != kdata->part_count) {
		dbg("kernel partition count mismatch: %u != %u.\n", kcount, kdata->part_count);
		return ERROR;
	}

	if (test_binary_manager_kernel_metadata(kdata, kcount) != OK) {
		return ERROR;
	}

	if (kdata->version == 0) {
		dbg("invalid kernel version.\n");
		return ERROR;
	}

	if (binary_manager_get_kbin_version(kdata->inuse_idx) != kdata->version) {
		dbg("kernel version mismatch for in-use partition: %u.\n", (unsigned int)kdata->inuse_idx);
		return ERROR;
	}

	if (binary_manager_verify_kbin(KERNEL_BIN_COUNT) != BINMGR_INVALID_PARAM) {
		dbg("binary_manager_verify_kbin accepted invalid partition index.\n");
		return ERROR;
	}

	if (binary_manager_get_kbin_version(KERNEL_BIN_COUNT) != 0) {
		dbg("binary_manager_get_kbin_version returned invalid partition version.\n");
		return ERROR;
	}

#ifdef CONFIG_APP_BINARY_SEPARATION
	if (binary_manager_get_index_with_name(NULL) != ERROR) {
		dbg("binary_manager_get_index_with_name accepted NULL name.\n");
		return ERROR;
	}

	if (binary_manager_get_index_with_name("missing") != ERROR) {
		dbg("binary_manager_get_index_with_name found a missing binary.\n");
		return ERROR;
	}

	if (binary_manager_get_udata(0) == NULL) {
		dbg("binary_manager_get_udata returned NULL for index 0.\n");
		return ERROR;
	}

	if (test_binary_manager_user_metadata() != OK) {
		return ERROR;
	}

	ucount = binary_manager_get_ucount();
	if (binary_manager_verify_ubin((int)ucount + 1, 0) != BINMGR_INVALID_PARAM) {
		dbg("binary_manager_verify_ubin accepted invalid binary index: %u.\n", ucount + 1);
		return ERROR;
	}

	if (binary_manager_get_ubin_version((int)ucount + 1, 0) != 0) {
		dbg("binary_manager_get_ubin_version returned invalid binary version: %u.\n", ucount + 1);
		return ERROR;
	}

	if (binary_manager_verify_ubin(-1, 0) != BINMGR_INVALID_PARAM) {
		dbg("binary_manager_verify_ubin accepted negative binary index.\n");
		return ERROR;
	}

	if (binary_manager_get_ubin_version(-1, 0) != 0) {
		dbg("binary_manager_get_ubin_version returned invalid binary version.\n");
		return ERROR;
	}
#endif

	return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_binary_manager(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_BINARY_MANAGER_TEST:
		ret = test_binary_manager_core(arg);
		break;
	}

	return ret;
}
