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

#include <tinyara/config.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mount.h>

#include <tinyara/fs/mtd.h>
#include <tinyara/fs/mksmartfs.h>

#define QEMU_SMARTFS_SIZE (256 * CONFIG_RAMMTD_ERASESIZE)
#define QEMU_BCH_SIZE     (16 * CONFIG_RAMMTD_ERASESIZE)

/* Keep independent backing stores alive for the lifetime of the registered
 * devices. Filesystem formatting and BCH raw writes must not corrupt /mnt.
 */

static uint8_t g_smart_storage[2][QEMU_SMARTFS_SIZE];
static uint8_t g_bch_storage[QEMU_BCH_SIZE];

static int qemu_smartfs_initialize(int minor, const char *devname)
{
	struct mtd_dev_s *mtd;
	int ret;

	mtd = rammtd_initialize(g_smart_storage[minor], QEMU_SMARTFS_SIZE);
	if (!mtd) {
		printf("QEMU storage: RAM MTD initialization failed for %s\n", devname);
		return -ENOMEM;
	}

	ret = smart_initialize(minor, mtd, NULL);
	if (ret < 0) {
		printf("QEMU storage: SMART initialization failed for %s: %d\n", devname, ret);
		return ret;
	}

	ret = mksmartfs(devname, false);
	if (ret < 0) {
		ret = -errno;
		printf("QEMU storage: format failed for %s: %d\n", devname, ret);
	}

	return ret;
}

int board_initialize(void)
{
	struct mtd_dev_s *mtd;
	int ret;

	ret = qemu_smartfs_initialize(0, "/dev/smart0");
	if (ret < 0) {
		return ret;
	}

	ret = mount("/dev/smart0", "/mnt", "smartfs", 0, NULL);
	if (ret < 0) {
		ret = -errno;
		printf("QEMU storage: /mnt mount failed: %d\n", ret);
		return ret;
	}

	/* The filesystem suite mounts and reformats this device itself. */

	ret = qemu_smartfs_initialize(1, "/dev/smart1");
	if (ret < 0) {
		return ret;
	}

	mtd = rammtd_initialize(g_bch_storage, QEMU_BCH_SIZE);
	if (!mtd) {
		printf("QEMU storage: RAM MTD initialization failed for BCH\n");
		return -ENOMEM;
	}

	ret = ftl_initialize(1, mtd);
	if (ret < 0) {
		printf("QEMU storage: FTL initialization failed: %d\n", ret);
		return ret;
	}

	printf("QEMU storage: /mnt ready, /dev/smart1 and /dev/mtdblock1 ready\n");
	return OK;
}
