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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unistd.h>

#include <tinyara/fs/fs.h>
#include <tinyara/fs/procfs.h>
#include <tinyara/os_api_test_drv.h>

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_PROCFS_SOURCE		"abcdef"
#define TEST_PROCFS_VERSION_PATH	PROCFS_MOUNT_POINT"/version"
#define TEST_PROCFS_VERSION_ENTRY	"version"
#define TEST_PROCFS_UPTIME_PATH		PROCFS_MOUNT_POINT"/uptime"
#define TEST_PROCFS_UPTIME_ENTRY	"uptime"
#define TEST_PROCFS_MISSING_PATH	PROCFS_MOUNT_POINT"/__missing__"
#define TEST_PROCFS_READ_BUFLEN		64
#define TEST_PROCFS_UPTIME_FIRST_READ	4
#define TEST_PROCFS_MAX_DIRENTS		(CONFIG_MAX_TASKS + 32)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_procfs_memcpy_helper(void)
{
	char dest[4];
	off_t offset;
	size_t copied;

	memset(dest, 0, sizeof(dest));
	offset = 2;
	copied = procfs_memcpy(TEST_PROCFS_SOURCE, sizeof(TEST_PROCFS_SOURCE) - 1, dest, 3, &offset);
	if (copied != 3 || offset != 0 || memcmp(dest, "cde", 3) != 0) {
		dbg("procfs_memcpy failed offset copy.\n");
		return ERROR;
	}

	memset(dest, 0, sizeof(dest));
	offset = 10;
	copied = procfs_memcpy(TEST_PROCFS_SOURCE, sizeof(TEST_PROCFS_SOURCE) - 1, dest, 3, &offset);
	if (copied != 0 || offset != 4) {
		dbg("procfs_memcpy failed skip-only copy.\n");
		return ERROR;
	}

	offset = sizeof(TEST_PROCFS_SOURCE) - 1;
	copied = procfs_memcpy(TEST_PROCFS_SOURCE, sizeof(TEST_PROCFS_SOURCE) - 1, dest, 3, &offset);
	if (copied != 0 || offset != 0) {
		dbg("procfs_memcpy failed EOF offset copy.\n");
		return ERROR;
	}

	return OK;
}

static int test_procfs_version_file(void)
{
	char buffer[TEST_PROCFS_READ_BUFLEN];
	ssize_t nread;
	int fd;

	fd = open(TEST_PROCFS_VERSION_PATH, O_RDONLY);
	if (fd < 0) {
		dbg("failed to open %s: %d.\n", TEST_PROCFS_VERSION_PATH, get_errno());
		return ERROR;
	}

	nread = read(fd, buffer, sizeof(buffer) - 1);
	if (nread <= 0) {
		dbg("failed to read %s: %d.\n", TEST_PROCFS_VERSION_PATH, get_errno());
		close(fd);
		return ERROR;
	}

	buffer[nread] = '\0';
	if (strncmp(buffer, "Version: ", strlen("Version: ")) != 0) {
		dbg("unexpected procfs version header: %s.\n", buffer);
		close(fd);
		return ERROR;
	}

	if (close(fd) != OK) {
		dbg("failed to close %s.\n", TEST_PROCFS_VERSION_PATH);
		return ERROR;
	}

	fd = open(TEST_PROCFS_VERSION_PATH, O_WRONLY);
	if (fd >= 0) {
		dbg("procfs version accepted write-only open.\n");
		close(fd);
		return ERROR;
	}

	fd = open(TEST_PROCFS_MISSING_PATH, O_RDONLY);
	if (fd >= 0) {
		dbg("procfs accepted missing file open.\n");
		close(fd);
		return ERROR;
	}

	return OK;
}

static int test_procfs_is_digit(char ch)
{
	return ch >= '0' && ch <= '9';
}

static int test_procfs_is_uptime_string(FAR const char *buffer)
{
	int frac_digits = 0;
	int seen_digit = 0;
	int seen_dot = 0;
	int i;

	for (i = 0; buffer[i] != '\0'; i++) {
		if (buffer[i] == ' ') {
			if (seen_digit || seen_dot) {
				return 0;
			}
			continue;
		}

		if (buffer[i] == '.') {
			if (!seen_digit || seen_dot) {
				return 0;
			}
			seen_dot = 1;
			continue;
		}

		if (!test_procfs_is_digit(buffer[i])) {
			return 0;
		}

		if (seen_dot) {
			frac_digits++;
		}
		seen_digit = 1;
	}

	return seen_digit && seen_dot && frac_digits >= 2;
}

static int test_procfs_uptime_file(void)
{
	char buffer[TEST_PROCFS_READ_BUFLEN];
	ssize_t nread;
	ssize_t total;
	int fd;

	fd = open(TEST_PROCFS_UPTIME_PATH, O_RDONLY);
	if (fd < 0) {
		dbg("failed to open %s: %d.\n", TEST_PROCFS_UPTIME_PATH, get_errno());
		return ERROR;
	}

	nread = read(fd, buffer, TEST_PROCFS_UPTIME_FIRST_READ);
	if (nread != TEST_PROCFS_UPTIME_FIRST_READ) {
		dbg("failed partial read for %s: %d.\n", TEST_PROCFS_UPTIME_PATH, get_errno());
		close(fd);
		return ERROR;
	}

	total = nread;
	nread = read(fd, &buffer[total], sizeof(buffer) - total - 1);
	if (nread <= 0) {
		dbg("failed second read for %s: %d.\n", TEST_PROCFS_UPTIME_PATH, get_errno());
		close(fd);
		return ERROR;
	}

	total += nread;
	buffer[total] = '\0';
	if (!test_procfs_is_uptime_string(buffer)) {
		dbg("unexpected procfs uptime: %s.\n", buffer);
		close(fd);
		return ERROR;
	}

	nread = read(fd, buffer, sizeof(buffer));
	if (nread != 0) {
		dbg("procfs uptime did not report EOF.\n");
		close(fd);
		return ERROR;
	}

	if (close(fd) != OK) {
		dbg("failed to close %s.\n", TEST_PROCFS_UPTIME_PATH);
		return ERROR;
	}

	fd = open(TEST_PROCFS_UPTIME_PATH, O_WRONLY);
	if (fd >= 0) {
		dbg("procfs uptime accepted write-only open.\n");
		close(fd);
		return ERROR;
	}

	return OK;
}

static int test_procfs_find_entry(FAR DIR *dirp, FAR const char *name)
{
	FAR struct dirent *entry;
	int count = 0;

	while (count < TEST_PROCFS_MAX_DIRENTS) {
		entry = readdir(dirp);
		if (entry == NULL) {
			return ERROR;
		}

		count++;
		if (strcmp(entry->d_name, name) == 0) {
			return DIRENT_ISFILE(entry->d_type) ? OK : ERROR;
		}
	}

	return ERROR;
}

static int test_procfs_directory(void)
{
	FAR struct dirent *entry;
	FAR DIR *dirp;
	int ret = ERROR;

	dirp = opendir(PROCFS_MOUNT_POINT);
	if (dirp == NULL) {
		dbg("failed to opendir %s: %d.\n", PROCFS_MOUNT_POINT, get_errno());
		return ERROR;
	}

	if (test_procfs_find_entry(dirp, TEST_PROCFS_VERSION_ENTRY) != OK) {
		dbg("failed to find %s in %s.\n", TEST_PROCFS_VERSION_ENTRY, PROCFS_MOUNT_POINT);
		goto errout_with_dir;
	}

	rewinddir(dirp);
	if (test_procfs_find_entry(dirp, TEST_PROCFS_UPTIME_ENTRY) != OK) {
		dbg("failed to find %s in %s.\n", TEST_PROCFS_UPTIME_ENTRY, PROCFS_MOUNT_POINT);
		goto errout_with_dir;
	}

	rewinddir(dirp);
	entry = readdir(dirp);
	if (entry == NULL) {
		dbg("failed to read %s after rewinddir.\n", PROCFS_MOUNT_POINT);
		goto errout_with_dir;
	}

	if (closedir(dirp) != OK) {
		dbg("failed to closedir %s.\n", PROCFS_MOUNT_POINT);
		return ERROR;
	}

	dirp = opendir(PROCFS_MOUNT_POINT);
	if (dirp == NULL) {
		dbg("failed to reopen %s: %d.\n", PROCFS_MOUNT_POINT, get_errno());
		return ERROR;
	}

	if (test_procfs_find_entry(dirp, TEST_PROCFS_VERSION_ENTRY) != OK) {
		dbg("failed to find %s after reopening %s.\n", TEST_PROCFS_VERSION_ENTRY, PROCFS_MOUNT_POINT);
		goto errout_with_dir;
	}

	ret = OK;

errout_with_dir:
	if (closedir(dirp) != OK) {
		dbg("failed to closedir %s.\n", PROCFS_MOUNT_POINT);
		ret = ERROR;
	}

	return ret;
}

static int test_procfs_metadata(void)
{
	struct stat st;
	struct statfs sfs;

	if (stat(PROCFS_MOUNT_POINT, &st) != OK || !S_ISDIR(st.st_mode)) {
		dbg("unexpected stat for %s.\n", PROCFS_MOUNT_POINT);
		return ERROR;
	}

	if (stat(TEST_PROCFS_VERSION_PATH, &st) != OK || !S_ISREG(st.st_mode)) {
		dbg("unexpected stat for %s.\n", TEST_PROCFS_VERSION_PATH);
		return ERROR;
	}

	if (stat(TEST_PROCFS_UPTIME_PATH, &st) != OK || !S_ISREG(st.st_mode)) {
		dbg("unexpected stat for %s.\n", TEST_PROCFS_UPTIME_PATH);
		return ERROR;
	}

	if (statfs(PROCFS_MOUNT_POINT, &sfs) != OK ||
			sfs.f_type != PROCFS_MAGIC ||
			sfs.f_namelen != NAME_MAX) {
		dbg("unexpected statfs for %s.\n", PROCFS_MOUNT_POINT);
		return ERROR;
	}

	return OK;
}

static int test_procfs_core(unsigned long arg)
{
	(void)arg;

	if (test_procfs_memcpy_helper() != OK) {
		return ERROR;
	}

	if (test_procfs_version_file() != OK) {
		return ERROR;
	}

	if (test_procfs_uptime_file() != OK) {
		return ERROR;
	}

	if (test_procfs_directory() != OK) {
		return ERROR;
	}

	return test_procfs_metadata();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_procfs(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_PROCFS_TEST:
		ret = test_procfs_core(arg);
		break;
	}

	return ret;
}
