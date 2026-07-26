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

#include <tinyara/fs/fs.h>
#include <tinyara/os_api_test_drv.h>

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_VFS_DATA		"os_api_test_vfs"
#define TEST_VFS_DEVPATH	"/dev/os_api_test_vfs"
#define TEST_VFS_IOCTL_CMD	0x5a51
#define TEST_VFS_IOCTL_ARG	0xa55a

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct test_vfs_state_s {
	int open_count;
	int close_count;
	int read_count;
	int write_count;
	int ioctl_count;
	int unlink_count;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct test_vfs_state_s g_test_vfs_state;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_vfs_open(FAR struct file *filep)
{
	g_test_vfs_state.open_count++;
	return OK;
}

static int test_vfs_close(FAR struct file *filep)
{
	g_test_vfs_state.close_count++;
	return OK;
}

static ssize_t test_vfs_read(FAR struct file *filep, FAR char *buffer, size_t buflen)
{
	size_t len = sizeof(TEST_VFS_DATA);

	g_test_vfs_state.read_count++;
	if (buflen < len) {
		return -EINVAL;
	}

	memcpy(buffer, TEST_VFS_DATA, len);
	return len;
}

static ssize_t test_vfs_write(FAR struct file *filep, FAR const char *buffer, size_t buflen)
{
	g_test_vfs_state.write_count++;
	if (buflen != sizeof(TEST_VFS_DATA) ||
			memcmp(buffer, TEST_VFS_DATA, sizeof(TEST_VFS_DATA)) != 0) {
		return -EINVAL;
	}

	return buflen;
}

static int test_vfs_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
	g_test_vfs_state.ioctl_count++;
	if (cmd != TEST_VFS_IOCTL_CMD || arg != TEST_VFS_IOCTL_ARG) {
		return -ENOTTY;
	}

	return OK;
}

static int test_vfs_unlink(FAR struct inode *inode)
{
	g_test_vfs_state.unlink_count++;
	return OK;
}

static const struct file_operations g_test_vfs_fops = {
	test_vfs_open,
	test_vfs_close,
	test_vfs_read,
	test_vfs_write,
	0,
	test_vfs_ioctl
#ifndef CONFIG_DISABLE_POLL
	, 0
#endif
	, test_vfs_unlink
};

static int test_vfs_validate_counts(void)
{
	if (g_test_vfs_state.open_count != 1 ||
			g_test_vfs_state.close_count != 1 ||
			g_test_vfs_state.read_count != 2 ||
			g_test_vfs_state.write_count != 2 ||
			g_test_vfs_state.ioctl_count != 2 ||
			g_test_vfs_state.unlink_count != 1) {
		dbg("unexpected vfs counters: open=%d close=%d read=%d write=%d ioctl=%d unlink=%d.\n",
			g_test_vfs_state.open_count,
			g_test_vfs_state.close_count,
			g_test_vfs_state.read_count,
			g_test_vfs_state.write_count,
			g_test_vfs_state.ioctl_count,
			g_test_vfs_state.unlink_count);
		return ERROR;
	}

	return OK;
}

static int test_vfs_core(unsigned long arg)
{
	char buffer[sizeof(TEST_VFS_DATA)];
	struct stat st;
	ssize_t nbytes;
	int fd;
	int ret;

	(void)arg;

	(void)unregister_driver(TEST_VFS_DEVPATH);
	memset(&g_test_vfs_state, 0, sizeof(g_test_vfs_state));

	ret = register_driver(TEST_VFS_DEVPATH, &g_test_vfs_fops, 0666, NULL);
	if (ret != OK) {
		dbg("register_driver failed: %d.\n", ret);
		return ERROR;
	}

	ret = register_driver(TEST_VFS_DEVPATH, &g_test_vfs_fops, 0666, NULL);
	if (ret == OK) {
		dbg("register_driver accepted duplicate path.\n");
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	ret = stat(TEST_VFS_DEVPATH, &st);
	if (ret != OK || !S_ISCHR(st.st_mode)) {
		dbg("stat failed or did not report character device.\n");
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	fd = open(TEST_VFS_DEVPATH, O_RDWR);
	if (fd < 0) {
		dbg("open failed: %d.\n", get_errno());
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	memset(buffer, 0, sizeof(buffer));
	nbytes = read(fd, buffer, sizeof(buffer));
	if (nbytes != sizeof(TEST_VFS_DATA) ||
			memcmp(buffer, TEST_VFS_DATA, sizeof(TEST_VFS_DATA)) != 0) {
		dbg("read returned unexpected data.\n");
		close(fd);
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	set_errno(0);
	nbytes = read(fd, buffer, sizeof(buffer) - 1);
	if (nbytes != ERROR || get_errno() != EINVAL) {
		dbg("short read buffer did not report EINVAL.\n");
		close(fd);
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	nbytes = write(fd, TEST_VFS_DATA, sizeof(TEST_VFS_DATA));
	if (nbytes != sizeof(TEST_VFS_DATA)) {
		dbg("write returned unexpected length: %d.\n", nbytes);
		close(fd);
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	set_errno(0);
	nbytes = write(fd, TEST_VFS_DATA, sizeof(TEST_VFS_DATA) - 1);
	if (nbytes != ERROR || get_errno() != EINVAL) {
		dbg("short write buffer did not report EINVAL.\n");
		close(fd);
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	ret = ioctl(fd, TEST_VFS_IOCTL_CMD, TEST_VFS_IOCTL_ARG);
	if (ret != OK) {
		dbg("ioctl failed.\n");
		close(fd);
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	set_errno(0);
	ret = ioctl(fd, -1, 0);
	if (ret != ERROR || get_errno() != ENOTTY) {
		dbg("invalid ioctl did not report ENOTTY.\n");
		close(fd);
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	if (close(fd) != OK) {
		dbg("close failed.\n");
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	if (unlink(TEST_VFS_DEVPATH) != OK) {
		dbg("unlink failed: %d.\n", get_errno());
		(void)unregister_driver(TEST_VFS_DEVPATH);
		return ERROR;
	}

	fd = open(TEST_VFS_DEVPATH, O_RDONLY);
	if (fd >= 0) {
		dbg("open accepted unlinked driver.\n");
		close(fd);
		return ERROR;
	}

	return test_vfs_validate_counts();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_vfs(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_VFS_TEST:
		ret = test_vfs_core(arg);
		break;
	}

	return ret;
}
