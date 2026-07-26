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
#include <sys/prctl.h>

#include <tinyara/mm/mm.h>
#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>

#include "debug/debug.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_mem_leak_checker_invalid_target(unsigned long arg)
{
	FAR struct tcb_s *self;

	(void)arg;

	self = sched_self();
	if (self == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	if (run_mem_leak_checker(self->pid, "__missing__") != ERROR) {
		dbg("run_mem_leak_checker accepted missing binary name.\n");
		return ERROR;
	}

	if (run_mem_leak_checker(self->pid, "") != ERROR) {
		dbg("run_mem_leak_checker accepted an empty binary name.\n");
		return ERROR;
	}

	return OK;
}

static int test_mem_leak_checker_kernel_heap(unsigned long arg)
{
	FAR struct tcb_s *self;

	(void)arg;

	self = sched_self();
	if (self == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	if (run_mem_leak_checker(self->pid, "kernel") != OK) {
		dbg("run_mem_leak_checker failed for kernel heap.\n");
		return ERROR;
	}

	return OK;
}

static int test_mem_leak_checker_prctl_all(unsigned long arg)
{
	FAR struct tcb_s *self;

	(void)arg;

	self = sched_self();
	if (self == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	if (prctl(PR_MEM_LEAK_CHECKER, self->pid) != OK) {
		dbg("PR_MEM_LEAK_CHECKER failed.\n");
		return ERROR;
	}

	return OK;
}

static int test_mem_leak_checker_core(unsigned long arg)
{
	if (test_mem_leak_checker_invalid_target(arg) != OK) {
		return ERROR;
	}

	if (test_mem_leak_checker_kernel_heap(arg) != OK) {
		return ERROR;
	}

	return test_mem_leak_checker_prctl_all(arg);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_mem_leak_checker(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_MEM_LEAK_CHECKER_TEST:
		ret = test_mem_leak_checker_core(arg);
		break;
	}

	return ret;
}
