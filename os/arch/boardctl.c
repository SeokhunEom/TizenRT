/****************************************************************************
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
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
 * configs/boardctl.c
 *
 *   Copyright (C) 2015-2016 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <tinyara/config.h>

#include <sys/types.h>
#include <sys/boardctl.h>
#include <sys/stat.h>
#include <debug.h>
#include <sched.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <tinyara/board.h>
#include <tinyara/arch.h>
#ifdef CONFIG_SYSTEM_REBOOT_REASON
#include <tinyara/reboot_reason.h>
#endif
#ifdef CONFIG_ARCH_BOARD_BK7239N
#include <driver/flash.h>
#endif

#ifdef CONFIG_LIB_BOARDCTL
/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#if defined(CONFIG_ARCH_BOARD_BK7239N) && defined(CONFIG_BOARDCTL_RESET)
#define BK7239N_BOOTLOADER_PATH          "/mnt/bootloader.bin"
#define BK7239N_BOOTLOADER_FLASH_ADDR    0x0
#define BK7239N_BOOTLOADER_MAX_SIZE      0x11000
#define BK7239N_BOOTLOADER_EXPECTED_SIZE BK7239N_BOOTLOADER_MAX_SIZE
#define BK7239N_BOOTLOADER_ERASE_SIZE    0x1000
#define BK7239N_BOOTLOADER_WRITE_SIZE    256
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/
#if defined(CONFIG_ARCH_BOARD_BK7239N) && defined(CONFIG_BOARDCTL_RESET)
static int boardctl_bk7239n_flash_error(bk_err_t err)
{
	lldbg("bootloader download flash error: %d\n", err);
	return -EIO;
}

static int boardctl_bk7239n_validate_bootloader_image(int fd, uint32_t image_size,
													  uint8_t *buf)
{
	uint32_t offset;

	for (offset = 0; offset < image_size; offset += BK7239N_BOOTLOADER_WRITE_SIZE) {
		uint32_t chunk = image_size - offset;
		ssize_t nread;

		if (chunk > BK7239N_BOOTLOADER_WRITE_SIZE) {
			chunk = BK7239N_BOOTLOADER_WRITE_SIZE;
		}

		nread = read(fd, buf, chunk);
		if (nread != (ssize_t)chunk) {
			lldbg("bootloader pre-read failed at 0x%x: %d/%u, errno: %d\n",
				  offset, (int)nread, chunk, errno);
			return -EIO;
		}
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		lldbg("bootloader seek failed: %s, errno: %d\n",
			  BK7239N_BOOTLOADER_PATH, errno);
		return errno > 0 ? -errno : -EIO;
	}

	return OK;
}

static int boardctl_bk7239n_download_bootloader(void)
{
	struct stat st;
	uint8_t write_buf[BK7239N_BOOTLOADER_WRITE_SIZE];
	uint8_t verify_buf[BK7239N_BOOTLOADER_WRITE_SIZE];
	uint32_t image_size;
	uint32_t offset;
	int fd;
	int ret;

	if (stat(BK7239N_BOOTLOADER_PATH, &st) < 0) {
		if (errno == ENOENT || errno == ENOTDIR) {
			return OK;
		}

		lldbg("bootloader stat failed: %s, errno: %d\n",
			  BK7239N_BOOTLOADER_PATH, errno);
		return errno > 0 ? -errno : -EIO;
	}

	if (st.st_size <= 0) {
		lldbg("bootloader is empty: %s\n", BK7239N_BOOTLOADER_PATH);
		return -EINVAL;
	}

	if (st.st_size > BK7239N_BOOTLOADER_MAX_SIZE) {
		lldbg("bootloader is too large: %d > %d\n",
			  (int)st.st_size, BK7239N_BOOTLOADER_MAX_SIZE);
		return -EFBIG;
	}

	if (st.st_size != BK7239N_BOOTLOADER_EXPECTED_SIZE) {
		lldbg("bootloader size mismatch: %d != %d\n",
			  (int)st.st_size, BK7239N_BOOTLOADER_EXPECTED_SIZE);
		return -EINVAL;
	}

	image_size = (uint32_t)st.st_size;
	fd = open(BK7239N_BOOTLOADER_PATH, O_RDONLY);
	if (fd < 0) {
		lldbg("bootloader open failed: %s, errno: %d\n",
			  BK7239N_BOOTLOADER_PATH, errno);
		return errno > 0 ? -errno : -EIO;
	}

	ret = boardctl_bk7239n_validate_bootloader_image(fd, image_size, write_buf);
	if (ret < 0) {
		goto errout_with_close;
	}

	ret = bk_flash_driver_init();
	if (ret != BK_OK) {
		ret = boardctl_bk7239n_flash_error(ret);
		goto errout_with_close;
	}

	bk_flash_set_bootloader_update_allowed(true);

	for (offset = 0; offset < BK7239N_BOOTLOADER_MAX_SIZE;
		 offset += BK7239N_BOOTLOADER_ERASE_SIZE) {
		ret = bk_flash_erase_sector(BK7239N_BOOTLOADER_FLASH_ADDR + offset);
		if (ret != BK_OK) {
			ret = boardctl_bk7239n_flash_error(ret);
			goto errout_with_permission;
		}
	}

	for (offset = 0; offset < image_size; offset += BK7239N_BOOTLOADER_WRITE_SIZE) {
		uint32_t chunk = image_size - offset;
		ssize_t nread;

		if (chunk > BK7239N_BOOTLOADER_WRITE_SIZE) {
			chunk = BK7239N_BOOTLOADER_WRITE_SIZE;
		}

		nread = read(fd, write_buf, chunk);
		if (nread != (ssize_t)chunk) {
			lldbg("bootloader read failed at 0x%x: %d/%u, errno: %d\n",
				  offset, (int)nread, chunk, errno);
			ret = -EIO;
			goto errout_with_permission;
		}

		ret = bk_flash_write_bytes(BK7239N_BOOTLOADER_FLASH_ADDR + offset,
								   write_buf, chunk);
		if (ret != BK_OK) {
			ret = boardctl_bk7239n_flash_error(ret);
			goto errout_with_permission;
		}

		ret = bk_flash_read_bytes(BK7239N_BOOTLOADER_FLASH_ADDR + offset,
								  verify_buf, chunk);
		if (ret != BK_OK) {
			ret = boardctl_bk7239n_flash_error(ret);
			goto errout_with_permission;
		}

		if (memcmp(write_buf, verify_buf, chunk) != 0) {
			lldbg("bootloader verify failed at 0x%x\n", offset);
			ret = -EIO;
			goto errout_with_permission;
		}
	}

	bk_flash_set_bootloader_update_allowed(false);
	close(fd);
	lldbg("bootloader download complete: %u bytes\n", image_size);
	return OK;

errout_with_permission:
	bk_flash_set_bootloader_update_allowed(false);
errout_with_close:
	close(fd);
	return ret;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: boardctl
 *
 * Description:
 *   In a small embedded system, there will typically be a much greater
 *   interaction between application and low-level board features.  The
 *   canonically correct to implement such interactions is by implementing a
 *   character driver and performing the interactions via low level ioctl
 *   calls.  This, however, may not be practical in many cases and will lead
 *   to "correct" but awkward implementations.
 *
 *   boardctl() is non-standard OS interface to alleviate the problem.  It
 *   basically circumvents the normal device driver ioctl interlace and allows
 *   the application to perform direction IOCTL-like calls to the board-specific
 *   logic.  In it is especially useful for setting up board operational and
 *   test configurations.
 *
 * Input Parameters:
 *   cmd - Identifies the board command to be executed
 *   arg - The argument that accompanies the command.  The nature of the
 *         argument is determined by the specific command.
 *
 * Returned Value:
 *   On success zero (OK) is returned; -1 (ERROR) is returned on failure
 *   with the errno variable to indicate the nature of the failure.
 *
 ****************************************************************************/
int boardctl(unsigned int cmd, uintptr_t arg)
{
	int ret;

	switch (cmd) {
	/*
	 * CMD:           BOARDIOC_INIT
	 * DESCRIPTION:   Perform one-time application initialization.
	 * ARG:           The boardctl() argument is passed to the
	 *                board_app_initialize() implementation without modification.
	 *                The argument has no meaning to NuttX; the meaning of the
	 *                argument is a contract between the board-specific
	 *                initalization logic and the matching application logic.
	 *                The value cold be such things as a mode enumeration value,
	 *                a set of DIP switch switch settings, a pointer to
	 *                configuration data read from a file or serial FLASH, or
	 *                whatever you would like to do with it.  Every
	 *                implementation should accept zero/NULL as a default
	 *                configuration.
	 * CONFIGURATION: CONFIG_LIB_BOARDCTL
	 * DEPENDENCIES:  Board logic must provide board_app_initialization
	 */
	case BOARDIOC_INIT:
		ret = board_app_initialize();
		break;

#ifdef CONFIG_BOARDCTL_POWEROFF
	/*
	 * CMD:           BOARDIOC_POWEROFF
	 * DESCRIPTION:   Power off the board
	 * ARG:           Integer value providing power off status information
	 * CONFIGURATION: CONFIG_BOARDCTL_POWEROFF
	 * DEPENDENCIES:  Board logic must provide board_power_off
	 */
	case BOARDIOC_POWEROFF:
		ret = board_power_off((int)arg);
		break;
#endif

#ifdef CONFIG_BOARDCTL_RESET
	/*
	 * CMD:           BOARDIOC_RESET
	 * DESCRIPTION:   Reset the board
	 * ARG:           Integer value providing power off status information
	 * CONFIGURATION: CONFIG_BOARDCTL_RESET
	 * DEPENDENCIES:  Board logic must provide board_reset
	 */
	case BOARDIOC_RESET:
		/* To reboot the board, we will do nothing. */
		sched_lock();
		/* Add 100ms delay for flushing another logs like printf. */
		up_mdelay(100);
		lldbg("Board will Reboot now. pid: %d\n", getpid());
#ifdef CONFIG_ARCH_BOARD_BK7239N
		ret = boardctl_bk7239n_download_bootloader();
		if (ret < 0) {
			sched_unlock();
			break;
		}
#endif
#ifdef CONFIG_SYSTEM_REBOOT_REASON
		if (!up_reboot_reason_is_written()) {
			for (int i = 0; i < 10; i++) {
				lldbg("\n    VIOLATION!!! YOU MUST SET REBOOT REASON!!!\n\n");
			}
			up_reboot_reason_write(REBOOT_SYSTEM_WITHOUT_SET_REASON);
		}
#endif
		/* Add 100ms delay for flushing UART FIFO. */
		up_mdelay(100);
		ret = board_reset((int)arg);

		sched_unlock();
		break;
#endif

#ifdef CONFIG_BOARDCTL_UNIQUEID
	/*
	 * CMD:           BOARDIOC_UNIQUEID
	 * DESCRIPTION:   Return a unique ID associated with the board (such
	 *                as a serial number or a MAC address).
	 * ARG:           A writable array of size CONFIG_BOARDCTL_UNIQUEID_SIZE
	 *                in which to receive the board unique ID.
	 * DEPENDENCIES:  Board logic must provide the board_uniqueid()
	 *                interface.
	 */
	case BOARDIOC_UNIQUEID:
		ret = board_uniqueid((FAR uint8_t *)arg);
		break;
#endif

	default:
		ret = -ENOTTY;
		break;
	}

	/* Set the errno value on any errors */
	if (ret < 0) {
		set_errno(-ret);
		return ERROR;
	}

	return OK;
}
#endif /* CONFIG_LIB_BOARDCTL */
