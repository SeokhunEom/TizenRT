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
#include <string.h>

#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_errno_access(unsigned long arg)
{
	FAR struct tcb_s *self;
	FAR int *perrno;
	int old_errno;

	(void)arg;

	self = sched_self();
	if (self == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	old_errno = get_errno();
	perrno = get_errno_ptr();
	if (perrno != &self->pterrno) {
		dbg("get_errno_ptr did not return current task errno.\n");
		return ERROR;
	}

	set_errno(EAGAIN);
	if (get_errno() != EAGAIN || *perrno != EAGAIN) {
		dbg("set_errno did not update current task errno.\n");
		goto errout;
	}

	*perrno = ENOMEM;
	if (get_errno() != ENOMEM) {
		dbg("get_errno did not read current task errno.\n");
		goto errout;
	}

	set_errno(old_errno);
	return OK;

errout:
	set_errno(old_errno);
	return ERROR;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_errno(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_ERRNO_TEST:
		ret = test_errno_access(arg);
		break;
	}

	return ret;
}
