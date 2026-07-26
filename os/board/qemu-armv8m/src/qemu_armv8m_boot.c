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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <debug.h>

#ifdef CONFIG_FLASH_PARTITION
#include "common.h"
#endif

#if defined(CONFIG_BINARY_MANAGER) && defined(CONFIG_USE_BP)
#include <tinyara/binary_manager.h>
#endif

#if defined(CONFIG_APP_BINARY_SEPARATION) && !defined(CONFIG_BINARY_MANAGER)
#include <crc32.h>
#include <arch/chip/chip.h>
#include <tinyara/binary_manager.h>
#include <tinyara/binfmt/binfmt.h>
#include <tinyara/binfmt/symtab.h>
#include <tinyara/kmalloc.h>

#include "binfmt_arch_apis.h"
#include "libelf.h"
#endif

#ifdef CONFIG_BUILTIN_APPS
#include <apps/builtin.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_APP_BINARY_SEPARATION) && !defined(CONFIG_BINARY_MANAGER)
#ifdef CONFIG_XIP_ELF
#define QEMU_COMMON_LOADADDR      0x102c0000
#define QEMU_APP1_LOADADDR        0x10360000
#define QEMU_COMMON_PATH          "/tmp/common"
#else
#define QEMU_APP1_LOADADDR        0x10300000
#endif

#define QEMU_APP1_PATH            "/tmp/app1"
#define QEMU_BINARY_CHECKSUM_SIZE 4
#define QEMU_SSRAM_END            (MPS2_AN505_SSRAM_BASE + MPS2_AN505_SSRAM_SIZE)
#endif

#if defined(CONFIG_APP_BINARY_SEPARATION) && !defined(CONFIG_BINARY_MANAGER)
struct qemu_binary_slot_s {
	FAR const char *name;
	uintptr_t loadaddr;
	size_t capacity;
	FAR const char *path;
};
#endif

#if defined(CONFIG_APP_BINARY_SEPARATION) && !defined(CONFIG_BINARY_MANAGER)
#ifdef CONFIG_XIP_ELF
static const struct qemu_binary_slot_s g_qemu_common_slot = {
	"common",
	QEMU_COMMON_LOADADDR,
	QEMU_APP1_LOADADDR - QEMU_COMMON_LOADADDR,
	QEMU_COMMON_PATH,
};
#endif

static const struct qemu_binary_slot_s g_qemu_app1_slot = {
	"app1",
	QEMU_APP1_LOADADDR,
	QEMU_SSRAM_END - QEMU_APP1_LOADADDR,
	QEMU_APP1_PATH,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_APP_BINARY_SEPARATION) && !defined(CONFIG_BINARY_MANAGER)
static int qemu_armv8m_reject(FAR const struct qemu_binary_slot_s *slot,
							  FAR const char *reason, int result)
{
	lldbg("QEMU_LOAD_REJECT %s %s\n", slot->name, reason);
	return result;
}

static void qemu_armv8m_remove_binary(
	FAR const struct qemu_binary_slot_s *slot)
{
	if (unlink(slot->path) < 0) {
		lldbg("QEMU_LOAD_REJECT %s unlink\n", slot->name);
	}
}

static int qemu_armv8m_validate_package(
	FAR const struct qemu_binary_slot_s *slot, uint16_t header_size,
	size_t expected_header_size, uint32_t bin_size, uint32_t expected_crc,
	FAR size_t *package_size)
{
	FAR const uint8_t *package = (FAR const uint8_t *)slot->loadaddr;
	size_t prefix_size;
	uint32_t calculated_crc;

	if (slot->capacity < QEMU_BINARY_CHECKSUM_SIZE ||
		header_size != expected_header_size) {
		return qemu_armv8m_reject(slot, "header", -EINVAL);
	}

	if (bin_size == 0 ||
		header_size > slot->capacity - QEMU_BINARY_CHECKSUM_SIZE) {
		return qemu_armv8m_reject(slot, "size", -EFBIG);
	}

	prefix_size = QEMU_BINARY_CHECKSUM_SIZE + header_size;
	if (bin_size > slot->capacity - prefix_size) {
		return qemu_armv8m_reject(slot, "size", -EFBIG);
	}

	*package_size = prefix_size + bin_size;
	calculated_crc = crc32part(package + QEMU_BINARY_CHECKSUM_SIZE,
							   *package_size - QEMU_BINARY_CHECKSUM_SIZE, 0);
	if (calculated_crc != expected_crc) {
		return qemu_armv8m_reject(slot, "crc", -EBADMSG);
	}

	return OK;
}

static int qemu_armv8m_copy_binary(
	FAR const struct qemu_binary_slot_s *slot, size_t size)
{
	FAR const uint8_t *cursor = (FAR const uint8_t *)slot->loadaddr;
	size_t remaining = size;
	int fd;
	int ret;

	fd = open(slot->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		return qemu_armv8m_reject(slot, "open", fd);
	}

	while (remaining > 0) {
		ssize_t written = write(fd, cursor, remaining);
		if (written <= 0) {
			ret = written < 0 ? (int)written : -EIO;
			(void)close(fd);
			ret = qemu_armv8m_reject(slot, "write", ret);
			qemu_armv8m_remove_binary(slot);
			return ret;
		}

		cursor += (size_t)written;
		remaining -= (size_t)written;
	}

	ret = close(fd);
	if (ret < 0) {
		ret = qemu_armv8m_reject(slot, "close", ret);
		qemu_armv8m_remove_binary(slot);
		return ret;
	}

	return OK;
}

static void qemu_armv8m_set_exports(FAR struct binary_s *bin)
{
	exec_getsymtab(&bin->exports, &bin->nexports);
}

static void qemu_armv8m_init_mem_protect(FAR struct binary_s *bin)
{
	elf_save_bin_section_addr(bin);
	binfmt_arch_init_mem_protect(bin);
}

#ifdef CONFIG_SUPPORT_COMMON_BINARY
extern uint32_t *g_umm_app_id;

static int qemu_armv8m_load_common(void)
{
	FAR const common_binary_header_t *header =
		(FAR const common_binary_header_t *)QEMU_COMMON_LOADADDR;
	FAR struct binary_s *bin;
	size_t package_size;
	int ret;

	ret = qemu_armv8m_validate_package(&g_qemu_common_slot,
			header->header_size,
			sizeof(common_binary_header_t) - QEMU_BINARY_CHECKSUM_SIZE,
			header->bin_size, header->crc_hash, &package_size);
	if (ret < 0) {
		return ret;
	}

	ret = qemu_armv8m_copy_binary(&g_qemu_common_slot, package_size);
	if (ret < 0) {
		return ret;
	}

	bin = (FAR struct binary_s *)kmm_zalloc(sizeof(struct binary_s));
	if (!bin) {
		ret = qemu_armv8m_reject(&g_qemu_common_slot, "alloc", -ENOMEM);
		qemu_armv8m_remove_binary(&g_qemu_common_slot);
		return ret;
	}

	bin->filename = QEMU_COMMON_PATH;
	bin->filelen = header->bin_size;
	bin->offset = QEMU_BINARY_CHECKSUM_SIZE + header->header_size;
	bin->binary_idx = 0;
	bin->bin_ver = header->version;
	bin->bin_name = CONFIG_COMMON_BINARY_NAME;
	bin->islibrary = true;
#ifdef CONFIG_HAVE_CXX
	bin->run_library_ctors = true;
#endif
	qemu_armv8m_set_exports(bin);
	g_lib_binp = bin;

	ret = load_module(bin);
	if (ret < 0) {
		g_lib_binp = NULL;
		kmm_free(bin);
		ret = qemu_armv8m_reject(&g_qemu_common_slot, "load", ret);
		qemu_armv8m_remove_binary(&g_qemu_common_slot);
		return ret;
	}

	qemu_armv8m_init_mem_protect(bin);
	g_umm_app_id = (uint32_t *)(bin->sections[BIN_DATA] + 4);

	lldbg("qemu-armv8m: loaded %s\n", CONFIG_COMMON_BINARY_NAME);
	return OK;
}
#endif

static int qemu_armv8m_load_app1(void)
{
	FAR const user_binary_header_t *header =
		(FAR const user_binary_header_t *)QEMU_APP1_LOADADDR;
	FAR struct binary_s *bin;
	size_t package_size;
	int ret;

	ret = qemu_armv8m_validate_package(&g_qemu_app1_slot,
			header->header_size,
			sizeof(user_binary_header_t) - QEMU_BINARY_CHECKSUM_SIZE,
			header->bin_size, header->crc_hash, &package_size);
	if (ret < 0) {
		return ret;
	}

	if (strncmp(header->bin_name, CONFIG_APP1_BIN_NAME, BIN_NAME_MAX) != 0) {
		return qemu_armv8m_reject(&g_qemu_app1_slot, "name", -EINVAL);
	}

	ret = qemu_armv8m_copy_binary(&g_qemu_app1_slot, package_size);
	if (ret < 0) {
		return ret;
	}

	bin = (FAR struct binary_s *)kmm_zalloc(sizeof(struct binary_s));
	if (!bin) {
		ret = qemu_armv8m_reject(&g_qemu_app1_slot, "alloc", -ENOMEM);
		qemu_armv8m_remove_binary(&g_qemu_app1_slot);
		return ret;
	}

	bin->filename = QEMU_APP1_PATH;
	bin->filelen = header->bin_size;
	bin->offset = QEMU_BINARY_CHECKSUM_SIZE + header->header_size;
	bin->stacksize = header->bin_stacksize;
	bin->priority = header->bin_priority;
	bin->binary_idx = 1;
	bin->bin_ver = header->bin_ver;
	bin->bin_name = CONFIG_APP1_BIN_NAME;
	bin->ramsize = header->bin_ramsize;
	qemu_armv8m_set_exports(bin);

	ret = load_module(bin);
	if (ret < 0) {
		kmm_free(bin);
		ret = qemu_armv8m_reject(&g_qemu_app1_slot, "load", ret);
		qemu_armv8m_remove_binary(&g_qemu_app1_slot);
		return ret;
	}

	qemu_armv8m_init_mem_protect(bin);

#ifdef CONFIG_SUPPORT_COMMON_BINARY
	if (g_lib_binp) {
		FAR uint32_t *heap_table =
			(FAR uint32_t *)(g_lib_binp->sections[BIN_DATA] + 8);
		heap_table[bin->binary_idx] = bin->sections[BIN_HEAP];
	}
#endif

	ret = exec_module(bin);
	if (ret < 0) {
		unload_module(bin);
		kmm_free(bin);
		ret = qemu_armv8m_reject(&g_qemu_app1_slot, "exec", ret);
		qemu_armv8m_remove_binary(&g_qemu_app1_slot);
		return ret;
	}

	lldbg("QEMU_APP1_STARTED pid=%d\n", ret);
	return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_BOARD_INITIALIZE
void board_initialize(void)
{
#ifdef CONFIG_FLASH_PARTITION
	{
		FAR struct mtd_dev_s *mtd;
		partition_info_t partinfo;
		int ret;

		mtd = mtd_initialize();
		ret = configure_mtd_partitions(mtd, 0, &partinfo);
		if (ret != OK) {
			lldbg("qemu-armv8m: failed to configure flash partitions\n");
			return;
		}

#if defined(CONFIG_BINARY_MANAGER) && defined(CONFIG_USE_BP)
		ret = binary_manager_check_bootparam_set();
		if (ret != OK) {
			ret = binary_manager_recover_bootparam_set();
			if (ret != OK) {
				lldbg("qemu-armv8m: failed to recover bootparam set, ret %d\n", ret);
				return;
			}
		}
#endif
	}
#endif
#if defined(CONFIG_BUILTIN_APPS) && !defined(CONFIG_APP_BINARY_SEPARATION)
	register_examples_cmds();
#endif
#if defined(CONFIG_APP_BINARY_SEPARATION) && !defined(CONFIG_BINARY_MANAGER)
#ifdef CONFIG_SUPPORT_COMMON_BINARY
	if (qemu_armv8m_load_common() < 0) {
		return;
	}
#endif
	(void)qemu_armv8m_load_app1();
#endif
}
#endif
