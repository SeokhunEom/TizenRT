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

#include <tinyara/binfmt/binfmt.h>
#include <tinyara/os_api_test_drv.h>

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_BINFMT_DUMMY_PATH	"/bin/os_api_test_binfmt_dummy"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int g_binfmt_load_count;
static int g_binfmt_unload_count;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_binfmt_dummy_load(FAR struct binary_s *bin)
{
	g_binfmt_load_count++;
	if (bin == NULL || bin->filename == NULL ||
			strcmp(bin->filename, TEST_BINFMT_DUMMY_PATH) != 0) {
		return -ENOEXEC;
	}

	return OK;
}

static int test_binfmt_dummy_unload(FAR struct binary_s *bin)
{
	g_binfmt_unload_count++;
	return OK;
}

static int test_binfmt_load_unload(FAR struct binfmt_s *dummy)
{
	struct binary_s bin;

	memset(&bin, 0, sizeof(bin));
	bin.filename = TEST_BINFMT_DUMMY_PATH;

	g_binfmt_load_count = 0;
	g_binfmt_unload_count = 0;

	if (load_module(&bin) != OK) {
		dbg("load_module failed for dummy binfmt.\n");
		return ERROR;
	}

	if (g_binfmt_load_count != 1 || bin.unload != dummy->unload) {
		dbg("load_module did not use dummy binfmt.\n");
		(void)unload_module(&bin);
		return ERROR;
	}

	if (unload_module(&bin) != OK) {
		dbg("unload_module failed for dummy binfmt.\n");
		return ERROR;
	}

	if (g_binfmt_unload_count != 1) {
		dbg("unload_module did not call dummy unload.\n");
		return ERROR;
	}

	return OK;
}

static int test_binfmt_core(unsigned long arg)
{
	static struct binfmt_s dummy = {
		.next = NULL,
		.load = test_binfmt_dummy_load,
		.unload = test_binfmt_dummy_unload,
	};

	int registered = 0;
	int ret = ERROR;

	(void)arg;

	if (register_binfmt(NULL) != -EINVAL) {
		dbg("register_binfmt accepted NULL.\n");
		goto errout;
	}

	if (unregister_binfmt(NULL) != -EINVAL) {
		dbg("unregister_binfmt accepted NULL.\n");
		goto errout;
	}

	if (unregister_binfmt(&dummy) != -EINVAL) {
		dbg("unregister_binfmt accepted unregistered handler.\n");
		goto errout;
	}

	if (register_binfmt(&dummy) != OK) {
		dbg("register_binfmt failed.\n");
		goto errout;
	}

	registered = 1;

	if (test_binfmt_load_unload(&dummy) != OK) {
		goto errout;
	}

	if (unregister_binfmt(&dummy) != OK) {
		dbg("unregister_binfmt failed.\n");
		goto errout;
	}

	registered = 0;

	if (dummy.next != NULL) {
		dbg("unregister_binfmt did not clear next pointer.\n");
		goto errout;
	}

	if (unregister_binfmt(&dummy) != -EINVAL) {
		dbg("unregister_binfmt accepted already removed handler.\n");
		goto errout;
	}

	ret = OK;

errout:
	if (registered) {
		(void)unregister_binfmt(&dummy);
	}

	return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_binfmt(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_BINFMT_TEST:
		ret = test_binfmt_core(arg);
		break;
	}

	return ret;
}
