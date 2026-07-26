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
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <tinyara/os_api_test_drv.h>
#include <tinyara/serial/tioctl.h>

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_TERMIOS_DEVPATH	"/dev/console"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_SERIAL_TERMIOS
static int test_termios_getattr(int fd, FAR struct termios *termiosp)
{
	return ioctl(fd, TCGETS, (unsigned long)termiosp);
}

static int test_termios_setattr(int fd, FAR const struct termios *termiosp)
{
	return ioctl(fd, TCSETS, (unsigned long)termiosp);
}

static int test_termios_core(unsigned long arg)
{
	struct termios original;
	struct termios current;
	struct termios modified;
	int unread;
	int writable;
	int fd;
	int ret = ERROR;
	bool restore = false;

	(void)arg;

	fd = open(TEST_TERMIOS_DEVPATH, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		dbg("open %s failed: %d.\n", TEST_TERMIOS_DEVPATH, get_errno());
		return ERROR;
	}

	if (test_termios_getattr(fd, &original) != OK) {
		dbg("tcgetattr failed: %d.\n", get_errno());
		goto errout;
	}

	memcpy(&modified, &original, sizeof(modified));
	modified.c_iflag ^= ICRNL;
	modified.c_oflag ^= ONLCR;
	modified.c_lflag ^= ECHO;

	if (test_termios_setattr(fd, &modified) != OK) {
		dbg("tcsetattr failed: %d.\n", get_errno());
		goto errout;
	}

	restore = true;

	if (test_termios_getattr(fd, &current) != OK) {
		dbg("tcgetattr after set failed: %d.\n", get_errno());
		goto errout;
	}

	if ((current.c_iflag & ICRNL) != (modified.c_iflag & ICRNL) ||
			(current.c_oflag & ONLCR) != (modified.c_oflag & ONLCR) ||
			(current.c_lflag & ECHO) != (modified.c_lflag & ECHO)) {
		dbg("termios flags were not preserved by TCSETS/TCGETS.\n");
		goto errout;
	}

	set_errno(0);
	if (test_termios_getattr(fd, NULL) == OK || get_errno() != EINVAL) {
		dbg("tcgetattr accepted NULL termios.\n");
		goto errout;
	}

	set_errno(0);
	if (test_termios_setattr(fd, NULL) == OK || get_errno() != EINVAL) {
		dbg("tcsetattr accepted NULL termios.\n");
		goto errout;
	}

	if (ioctl(fd, TCFLSH, TCIFLUSH) != OK ||
			ioctl(fd, TCFLSH, TCOFLUSH) != OK ||
			ioctl(fd, TCFLSH, TCIOFLUSH) != OK) {
		dbg("tcflush accepted selector failed: %d.\n", get_errno());
		goto errout;
	}

	set_errno(0);
	if (ioctl(fd, TCFLSH, 0xff) == OK || get_errno() != EINVAL) {
		dbg("tcflush accepted invalid selector.\n");
		goto errout;
	}

	set_errno(0);
	if (ioctl(fd, TCSETSW, (unsigned long)&current) == OK ||
			get_errno() != ENOTTY) {
		dbg("TCSETSW was unexpectedly handled.\n");
		goto errout;
	}

	set_errno(0);
	if (ioctl(fd, TCSETSF, (unsigned long)&current) == OK ||
			get_errno() != ENOTTY) {
		dbg("TCSETSF was unexpectedly handled.\n");
		goto errout;
	}

	unread = -1;
	if (ioctl(fd, FIONREAD, (unsigned long)&unread) != OK || unread < 0) {
		dbg("FIONREAD failed: %d unread=%d.\n", get_errno(), unread);
		goto errout;
	}

	writable = -1;
	if (ioctl(fd, FIONWRITE, (unsigned long)&writable) != OK || writable < 0) {
		dbg("FIONWRITE failed: %d writable=%d.\n", get_errno(), writable);
		goto errout;
	}

	ret = OK;

errout:
	if (restore && test_termios_setattr(fd, &original) != OK) {
		dbg("termios restore failed: %d.\n", get_errno());
		ret = ERROR;
	}

	if (close(fd) != OK) {
		dbg("close %s failed: %d.\n", TEST_TERMIOS_DEVPATH, get_errno());
		ret = ERROR;
	}

	return ret;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_termios(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_TERMIOS_TEST:
#ifdef CONFIG_SERIAL_TERMIOS
		ret = test_termios_core(arg);
#else
		ret = -ENOSYS;
#endif
		break;
	}

	return ret;
}
