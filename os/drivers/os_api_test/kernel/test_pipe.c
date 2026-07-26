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
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <tinyara/fs/ioctl.h>
#include <tinyara/os_api_test_drv.h>

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_PIPE_DATA		"os_api_test_pipe"
#define TEST_FIFO_PATH		"/dev/os_api_test_fifo"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_pipe_read_write(void)
{
	char buffer[sizeof(TEST_PIPE_DATA)];
	struct stat st;
	int fds[2];
	ssize_t nbytes;
	int ret = ERROR;

	if (pipe(fds) != OK) {
		dbg("pipe failed: %d.\n", get_errno());
		return ERROR;
	}

	if (fstat(fds[0], &st) != OK || !S_ISCHR(st.st_mode)) {
		dbg("pipe fstat failed or did not report character device.\n");
		goto errout_with_pipe;
	}

	nbytes = read(fds[0], buffer, 0);
	if (nbytes != 0) {
		dbg("zero-length pipe read returned unexpected result: %d.\n", nbytes);
		goto errout_with_pipe;
	}

	nbytes = write(fds[1], TEST_PIPE_DATA, 0);
	if (nbytes != 0) {
		dbg("zero-length pipe write returned unexpected result: %d.\n", nbytes);
		goto errout_with_pipe;
	}

	nbytes = write(fds[1], TEST_PIPE_DATA, sizeof(TEST_PIPE_DATA));
	if (nbytes != sizeof(TEST_PIPE_DATA)) {
		dbg("pipe write returned unexpected result: %d.\n", nbytes);
		goto errout_with_pipe;
	}

	memset(buffer, 0, sizeof(buffer));
	nbytes = read(fds[0], buffer, sizeof(buffer));
	if (nbytes != sizeof(TEST_PIPE_DATA) ||
			memcmp(buffer, TEST_PIPE_DATA, sizeof(TEST_PIPE_DATA)) != 0) {
		dbg("pipe read returned unexpected data.\n");
		goto errout_with_pipe;
	}

	if (ioctl(fds[0], PIPEIOC_POLICY, 0) != OK) {
		dbg("PIPEIOC_POLICY clear failed.\n");
		goto errout_with_pipe;
	}

	if (ioctl(fds[0], PIPEIOC_POLICY, 1) != OK) {
		dbg("PIPEIOC_POLICY failed.\n");
		goto errout_with_pipe;
	}

	set_errno(0);
	if (ioctl(fds[0], -1, 0) != ERROR || get_errno() != ENOTTY) {
		dbg("pipe invalid ioctl did not report ENOTTY.\n");
		goto errout_with_pipe;
	}

	ret = OK;

errout_with_pipe:
	close(fds[0]);
	close(fds[1]);
	return ret;
}

static int test_fifo_read_write(void)
{
	char buffer[sizeof(TEST_PIPE_DATA)];
	ssize_t nbytes;
	int fd;
	int ret = ERROR;

	fd = open(TEST_FIFO_PATH, O_RDWR);
	if (fd < 0) {
		dbg("fifo open failed: %d.\n", get_errno());
		return ERROR;
	}

	nbytes = write(fd, TEST_PIPE_DATA, sizeof(TEST_PIPE_DATA));
	if (nbytes != sizeof(TEST_PIPE_DATA)) {
		dbg("fifo write returned unexpected result: %d.\n", nbytes);
		goto errout_with_fifo;
	}

	memset(buffer, 0, sizeof(buffer));
	nbytes = read(fd, buffer, sizeof(buffer));
	if (nbytes != sizeof(TEST_PIPE_DATA) ||
			memcmp(buffer, TEST_PIPE_DATA, sizeof(TEST_PIPE_DATA)) != 0) {
		dbg("fifo read returned unexpected data.\n");
		goto errout_with_fifo;
	}

	ret = OK;

errout_with_fifo:
	close(fd);
	return ret;
}

static int test_fifo_register_unregister(void)
{
	struct stat st;
	int ret;

	(void)unlink(TEST_FIFO_PATH);

	ret = mkfifo(TEST_FIFO_PATH, 0666);
	if (ret != OK) {
		dbg("mkfifo failed: %d.\n", ret);
		return ERROR;
	}

	if (stat(TEST_FIFO_PATH, &st) != OK || !S_ISCHR(st.st_mode)) {
		dbg("fifo stat failed or did not report character device.\n");
		unlink(TEST_FIFO_PATH);
		return ERROR;
	}

	ret = test_fifo_read_write();
	if (ret != OK) {
		unlink(TEST_FIFO_PATH);
		return ERROR;
	}

	ret = mkfifo(TEST_FIFO_PATH, 0666);
	if (ret == OK) {
		dbg("mkfifo accepted existing path.\n");
		unlink(TEST_FIFO_PATH);
		return ERROR;
	}

	ret = unlink(TEST_FIFO_PATH);
	if (ret != OK) {
		dbg("fifo unlink failed: %d.\n", ret);
		return ERROR;
	}

	return OK;
}

static int test_pipe_core(unsigned long arg)
{
	(void)arg;

	if (test_pipe_read_write() != OK) {
		return ERROR;
	}

	return test_fifo_register_unregister();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_pipe(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_PIPE_TEST:
		ret = test_pipe_core(arg);
		break;
	}

	return ret;
}
