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

#ifdef CONFIG_APP_BINARY_SEPARATION
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

#ifdef CONFIG_APP_BINARY_SEPARATION
#ifdef CONFIG_XIP_ELF
#define QEMU_COMMON_LOADADDR      0x102c0000
#define QEMU_APP1_LOADADDR        0x10360000
#define QEMU_COMMON_PATH          "/tmp/common"
#else
#define QEMU_APP1_LOADADDR        0x10300000
#endif

#define QEMU_APP1_PATH            "/tmp/app1"
#define QEMU_BINARY_CHECKSUM_SIZE 4
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_APP_BINARY_SEPARATION
static int qemu_armv8m_copy_binary(FAR const char *path, FAR const void *src,
								   size_t size)
{
	FAR const uint8_t *cursor = (FAR const uint8_t *)src;
	size_t remaining = size;
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		lldbg("qemu-armv8m: failed to create %s: %d\n", path, fd);
		return fd;
	}

	while (remaining > 0) {
		ssize_t written = write(fd, cursor, remaining);
		if (written < 0) {
			lldbg("qemu-armv8m: failed to write %s: %d\n", path, written);
			close(fd);
			return (int)written;
		}

		cursor += written;
		remaining -= written;
	}

	close(fd);
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

	if (header->header_size != sizeof(common_binary_header_t) -
		QEMU_BINARY_CHECKSUM_SIZE ||
		header->bin_size == 0 ||
		header->bin_size > 8 * 1024 * 1024) {
		lldbg("qemu-armv8m: no common package at 0x%08x\n",
			  QEMU_COMMON_LOADADDR);
		return -ENOENT;
	}

	package_size = QEMU_BINARY_CHECKSUM_SIZE + header->header_size +
		header->bin_size;

	ret = qemu_armv8m_copy_binary(QEMU_COMMON_PATH,
								  (FAR const void *)QEMU_COMMON_LOADADDR,
								  package_size);
	if (ret < 0) {
		return ret;
	}

	bin = (FAR struct binary_s *)kmm_zalloc(sizeof(struct binary_s));
	if (!bin) {
		return -ENOMEM;
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
		lldbg("qemu-armv8m: failed to load %s: %d\n", QEMU_COMMON_PATH,
			  ret);
		g_lib_binp = NULL;
		kmm_free(bin);
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

	if (header->header_size != sizeof(user_binary_header_t) -
		QEMU_BINARY_CHECKSUM_SIZE ||
		header->bin_size == 0 ||
		header->bin_size > 8 * 1024 * 1024 ||
		strncmp(header->bin_name, CONFIG_APP1_BIN_NAME, BIN_NAME_MAX) != 0) {
		lldbg("qemu-armv8m: no app1 package at 0x%08x\n",
			  QEMU_APP1_LOADADDR);
		return -ENOENT;
	}

	package_size = QEMU_BINARY_CHECKSUM_SIZE + header->header_size +
		header->bin_size;

	ret = qemu_armv8m_copy_binary(QEMU_APP1_PATH,
								  (FAR const void *)QEMU_APP1_LOADADDR,
								  package_size);
	if (ret < 0) {
		return ret;
	}

	bin = (FAR struct binary_s *)kmm_zalloc(sizeof(struct binary_s));
	if (!bin) {
		return -ENOMEM;
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
		lldbg("qemu-armv8m: failed to load %s: %d\n", QEMU_APP1_PATH, ret);
		kmm_free(bin);
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
		lldbg("qemu-armv8m: failed to execute %s: %d\n",
			  QEMU_APP1_PATH, ret);
		unload_module(bin);
		kmm_free(bin);
		return ret;
	}

	lldbg("qemu-armv8m: started %s pid=%d\n", CONFIG_APP1_BIN_NAME, ret);
	return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_BOARD_INITIALIZE
void board_initialize(void)
{
#if defined(CONFIG_BUILTIN_APPS) && !defined(CONFIG_APP_BINARY_SEPARATION)
	register_examples_cmds();
#endif
#ifdef CONFIG_APP_BINARY_SEPARATION
#ifdef CONFIG_SUPPORT_COMMON_BINARY
	(void)qemu_armv8m_load_common();
#endif
	(void)qemu_armv8m_load_app1();
#endif
}
#endif
