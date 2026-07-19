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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#include <tinyara/config.h>

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#if !defined(CONFIG_FS_AUTOMOUNT_PROCFS)
#include <sys/mount.h>
#endif

#include <tinyara/fs/fs.h>

#include "utils_proc.h"

#define PROC_DUMP_BUFLEN 128
#define PROC_DUMP_PID_MAX 32767
#define PROC_DUMP_PID_COUNT (PROC_DUMP_PID_MAX + 1)
#define PROC_DUMP_BITMAP_BITS 8
#define PROC_DUMP_BITMAP_SIZE \
	((PROC_DUMP_PID_COUNT + PROC_DUMP_BITMAP_BITS - 1) / \
	 PROC_DUMP_BITMAP_BITS)
#define PROC_DUMP_CONVERGENCE_PASSES 2

static void proc_dump_help(void)
{
	printf("Usage:\n");
	printf("       proc_dump\n");
	printf("       proc_dump <pid>\n");
	printf("       proc_dump pid\n");
}

static int proc_dump_parse_pid(const char *value, unsigned int *pid)
{
	unsigned int parsed;
	int index;

	if (value == NULL || value[0] == '\0' || pid == NULL) {
		return ERROR;
	}

	parsed = 0;
	for (index = 0; value[index] != '\0'; index++) {
		unsigned int digit;

		if (value[index] < '0' || value[index] > '9') {
			return ERROR;
		}

		digit = (unsigned int)(value[index] - '0');
		if (parsed > (PROC_DUMP_PID_MAX - digit) / 10) {
			return ERROR;
		}
		parsed = parsed * 10 + digit;
	}

	*pid = parsed;
	return OK;
}

static bool proc_dump_is_transient(int error_code)
{
	return error_code == ENOENT || error_code == ENODEV;
}

static bool proc_dump_pid_was_dumped(const unsigned char *dumped,
					 unsigned int pid)
{
	return (dumped[pid / PROC_DUMP_BITMAP_BITS] &
			(1u << (pid % PROC_DUMP_BITMAP_BITS))) != 0;
}

static void proc_dump_mark_pid_dumped(unsigned char *dumped,
					  unsigned int pid)
{
	dumped[pid / PROC_DUMP_BITMAP_BITS] |=
		(1u << (pid % PROC_DUMP_BITMAP_BITS));
}

static char *proc_dump_child_path(const char *parent, const char *name)
{
	char *path;

	path = NULL;
	if (asprintf(&path, "%s/%s", parent, name) < 0) {
		free(path);
		return NULL;
	}

	return path;
}

static char *proc_dump_child_relative(const char *parent, const char *name)
{
	char *path;

	path = NULL;
	if (parent[0] == '\0') {
		if (asprintf(&path, "%s", name) < 0) {
			free(path);
			return NULL;
		}
	} else if (asprintf(&path, "%s/%s", parent, name) < 0) {
		free(path);
		return NULL;
	}

	return path;
}

static int proc_dump_file(unsigned int pid, const char *path,
				  const char *relative, int *error_code)
{
	char buf[PROC_DUMP_BUFLEN];
	int ret;

	printf("--- %s/%u/%s ---\n", PROCFS_MOUNT_POINT, pid, relative);
	errno = 0;
	ret = utils_readfile(path, buf, PROC_DUMP_BUFLEN, NULL, NULL);
	if (ret < 0) {
		*error_code = errno != 0 ? errno : EIO;
		printf("\n");
		return ERROR;
	}

	printf("\n");
	return OK;
}

static int proc_dump_walk(unsigned int pid, const char *directory,
				  const char *relative, int *error_code)
{
	DIR *dirp;
	struct dirent *entryp;
	char *child;
	char *child_relative;
	struct stat statbuf;
	int ret;
	int saved_errno;
	int close_errno;

	dirp = opendir(directory);
	if (dirp == NULL) {
		*error_code = errno != 0 ? errno : EIO;
		return ERROR;
	}

	ret = OK;
	saved_errno = 0;
	for (;;) {
		errno = 0;
		entryp = readdir(dirp);
		if (entryp == NULL) {
			if (errno != 0) {
				ret = ERROR;
				saved_errno = errno;
			}
			break;
		}

		if (strcmp(entryp->d_name, ".") == 0 ||
			strcmp(entryp->d_name, "..") == 0) {
			continue;
		}

		child = proc_dump_child_path(directory, entryp->d_name);
		if (child == NULL) {
			ret = ERROR;
			saved_errno = ENOMEM;
			break;
		}

		child_relative = proc_dump_child_relative(relative, entryp->d_name);
		if (child_relative == NULL) {
			free(child);
			ret = ERROR;
			saved_errno = ENOMEM;
			break;
		}

		if (stat(child, &statbuf) < 0) {
			ret = ERROR;
			saved_errno = errno != 0 ? errno : EIO;
		} else if (S_ISDIR(statbuf.st_mode)) {
			ret = proc_dump_walk(pid, child, child_relative, &saved_errno);
		} else if (S_ISREG(statbuf.st_mode)) {
			ret = proc_dump_file(pid, child, child_relative, &saved_errno);
		}

		free(child_relative);
		free(child);

		if (ret < 0) {
			break;
		}
	}

	close_errno = 0;
	if (closedir(dirp) < 0) {
		close_errno = errno != 0 ? errno : EIO;
	}

	if (close_errno != 0 &&
		(ret == OK || (proc_dump_is_transient(saved_errno) &&
		 !proc_dump_is_transient(close_errno)))) {
		ret = ERROR;
		saved_errno = close_errno;
	}

	if (ret < 0) {
		*error_code = saved_errno;
	}

	return ret;
}

static int proc_dump_pid(unsigned int pid, int *error_code)
{
	char *directory;
	struct stat statbuf;
	int ret;

	printf("=== PID %u ===\n", pid);
	directory = NULL;
	if (asprintf(&directory, "%s/%u", PROCFS_MOUNT_POINT, pid) < 0) {
		free(directory);
		*error_code = ENOMEM;
		return ERROR;
	}

	ret = proc_dump_walk(pid, directory, "", error_code);
	if (ret == OK) {
		errno = 0;
		if (stat(directory, &statbuf) < 0) {
			*error_code = errno != 0 ? errno : EIO;
			ret = ERROR;
		}
	}
	free(directory);
	return ret;
}

static int proc_dump_all(void)
{
	unsigned char *dumped;
	unsigned int converged_passes;
	DIR *dirp;
	struct dirent *entryp;
	unsigned int pid;
	bool found_new_pid;
	int result;
	int error_code;
	int readdir_errno;
	int close_errno;

	dumped = (unsigned char *)calloc(PROC_DUMP_BITMAP_SIZE,
								 sizeof(*dumped));
	if (dumped == NULL) {
		printf("Failed to allocate PID tracking bitmap\n");
		return ERROR;
	}

	result = OK;
	converged_passes = 0;
	while (converged_passes < PROC_DUMP_CONVERGENCE_PASSES) {
		found_new_pid = false;
		dirp = opendir(PROCFS_MOUNT_POINT);
		if (dirp == NULL) {
			error_code = errno != 0 ? errno : EIO;
			printf("Failed to open procfs, errno : %d\n", error_code);
			result = ERROR;
			converged_passes++;
			continue;
		}

		readdir_errno = 0;
		for (;;) {
			errno = 0;
			entryp = readdir(dirp);
			if (entryp == NULL) {
				readdir_errno = errno;
				break;
			}

			if (!DIRENT_ISDIRECTORY(entryp->d_type) ||
				proc_dump_parse_pid(entryp->d_name, &pid) < 0 ||
				proc_dump_pid_was_dumped(dumped, pid)) {
				continue;
			}

			proc_dump_mark_pid_dumped(dumped, pid);
			found_new_pid = true;
			error_code = 0;
			if (proc_dump_pid(pid, &error_code) < 0 &&
				!proc_dump_is_transient(error_code)) {
				printf("Failed to dump PID %u, errno : %d\n", pid,
					   error_code);
				result = ERROR;
			}
		}

		if (readdir_errno != 0) {
			printf("Failed to read procfs, errno : %d\n", readdir_errno);
			result = ERROR;
		}

		close_errno = 0;
		if (closedir(dirp) < 0) {
			close_errno = errno != 0 ? errno : EIO;
		}
		if (close_errno != 0) {
			printf("Failed to close procfs, errno : %d\n", close_errno);
			result = ERROR;
		}

		if (found_new_pid) {
			converged_passes = 0;
		} else {
			converged_passes++;
		}
	}

	free(dumped);
	return result;
}

int utils_proc_dump(int argc, char **args)
{
	unsigned int pid;
	struct statfs statfsbuf;
	int error_code;
	int ret;
	bool dump_all;
#if !defined(CONFIG_FS_AUTOMOUNT_PROCFS)
	bool mounted_procfs;

	mounted_procfs = false;
#endif

	if (argc == 1) {
		proc_dump_help();
		return OK;
	}

	if (argc != 2 || args == NULL || args[1] == NULL) {
		proc_dump_help();
		return ERROR;
	}

	dump_all = strcmp(args[1], "pid") == 0;
	if (!dump_all && proc_dump_parse_pid(args[1], &pid) < 0) {
		proc_dump_help();
		return ERROR;
	}

#if !defined(CONFIG_FS_AUTOMOUNT_PROCFS)
	ret = mount(NULL, PROCFS_MOUNT_POINT, PROCFS_FSTYPE, 0, NULL);
	if (ret == ERROR) {
		if (errno != EEXIST) {
			printf("Failed to mount procfs : %d\n", errno);
			return ERROR;
		}
	} else {
		mounted_procfs = true;
	}
#endif

	memset(&statfsbuf, 0, sizeof(statfsbuf));
	if (statfs(PROCFS_MOUNT_POINT, &statfsbuf) < 0) {
		error_code = errno != 0 ? errno : EIO;
		printf("Failed to statfs procfs : %d\n", error_code);
		ret = ERROR;
		goto cleanup;
	}
	if (statfsbuf.f_type != PROCFS_MAGIC) {
		printf("Unexpected filesystem at %s\n", PROCFS_MOUNT_POINT);
		ret = ERROR;
		goto cleanup;
	}

	if (dump_all) {
		ret = proc_dump_all();
	} else {
		error_code = 0;
		ret = proc_dump_pid(pid, &error_code);
		if (ret < 0) {
			if (proc_dump_is_transient(error_code)) {
				printf("PID %u not found\n", pid);
			} else {
				printf("Failed to dump PID %u, errno : %d\n", pid,
					   error_code);
			}
		}
	}

cleanup:
#if !defined(CONFIG_FS_AUTOMOUNT_PROCFS)
	if (mounted_procfs) {
		/* VFS has no unmount-by-handle API; a concurrent mount replacement
		 * can race this path-based cleanup.
		 */
		if (umount(PROCFS_MOUNT_POINT) < 0) {
			error_code = errno;
			printf("Failed to unmount procfs : %d\n", error_code);
			ret = ERROR;
		}
	}
#endif

	return ret;
}
