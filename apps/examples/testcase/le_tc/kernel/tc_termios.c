/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
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

/// @file tc_termios.c

/// @brief Test Case Example for Termios API

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <tinyara/config.h>

#include <sys/ioctl.h>

#include <tinyara/os_api_test_drv.h>

#include "tc_internal.h"

/**
* @fn                   :tc_termios_tcsetattr_tcgetattr
* @brief                :Check serial termios ioctl path in kernel
* API's covered         :TCGETS, TCSETS, TCSETSW, TCSETSF, TCFLSH,
*                       :FIONREAD, FIONWRITE
* Preconditions         :CONFIG_SERIAL_TERMIOS
* Postconditions        :none
* @return               :void
*/
static void tc_termios_tcsetattr_tcgetattr(void)
{
	int ret_chk;

	ret_chk = ioctl(tc_get_drvfd(), TESTIOC_TERMIOS_TEST, 0);
	TC_ASSERT_EQ("termios", ret_chk, OK);

	TC_SUCCESS_RESULT();
}

/****************************************************************************
 * Name: termios
 ****************************************************************************/

int termios_main(void)
{
	tc_termios_tcsetattr_tcgetattr();
	return 0;
}
