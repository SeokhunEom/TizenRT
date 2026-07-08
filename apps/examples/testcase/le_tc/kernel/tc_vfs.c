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

#include <stdio.h>
#include <sys/ioctl.h>

#include <tinyara/os_api_test_drv.h>

#include "tc_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void tc_vfs_kernel(void)
{
	int ret;

	ret = ioctl(tc_get_drvfd(), TESTIOC_VFS_TEST, 0);
	TC_ASSERT_EQ("vfs", ret, OK);

	TC_SUCCESS_RESULT();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int vfs_main(void)
{
	tc_vfs_kernel();

	return 0;
}
