/****************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
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

#include <string.h>
#include <debug.h>
#include <errno.h>
#if defined(CONFIG_BINARY_MANAGER) && !defined(CONFIG_DISABLE_SIGNALS)
#include <signal.h>
#endif

#include <tinyara/kmalloc.h>
#include <tinyara/sched.h>
#include <tinyara/binfmt/binfmt.h>
#include <tinyara/binary_manager.h>
#ifdef CONFIG_SUPPORT_COMMON_BINARY
#include <tinyara/spinlock.h>
#endif

#ifdef CONFIG_SAVE_BIN_SECTION_ADDR
#include "libelf/libelf.h"
#endif

#include "binfmt.h"
#include "binfmt_arch_apis.h"
#include "binary_manager/binary_manager_internal.h"

#ifdef CONFIG_BINFMT_ENABLE

#include <tinyara/mm/mm.h>

#ifdef CONFIG_SUPPORT_COMMON_BINARY
struct binary_s *g_lib_binp;
static FAR uint32_t *g_umm_app_id;
static spinlock_t g_umm_app_id_lock;

FAR uint32_t *binfmt_exchange_umm_app_id(FAR uint32_t *address)
{
	FAR uint32_t *previous;
	irqstate_t flags = spin_lock_irqsave(&g_umm_app_id_lock);

	previous = g_umm_app_id;
	g_umm_app_id = address;
	spin_unlock_irqrestore(&g_umm_app_id_lock, flags);
	return previous;
}

int binfmt_umm_app_id_is(FAR uint32_t *address)
{
	irqstate_t flags = spin_lock_irqsave(&g_umm_app_id_lock);
	int matches = g_umm_app_id == address;

	spin_unlock_irqrestore(&g_umm_app_id_lock, flags);
	return matches;
}

void binfmt_update_umm_app_id(uint32_t app_id)
{
	irqstate_t flags = spin_lock_irqsave(&g_umm_app_id_lock);

	if (g_umm_app_id != NULL) {
		*g_umm_app_id = app_id;
	}
	spin_unlock_irqrestore(&g_umm_app_id_lock, flags);
}
#endif

#if defined(CONFIG_SUPPORT_COMMON_BINARY) && \
	defined(CONFIG_APP_BINARY_SEPARATION) && defined(__KERNEL__)
static bool binfmt_common_domain_ready(FAR void *descriptor)
{
	FAR struct binary_s *bin = descriptor;
	int index = bin->binary_idx;

	return g_lib_binp == bin &&
		binfmt_umm_app_id_is((uint32_t *)(bin->sections[BIN_DATA] + 4)) &&
		BIN_STATE(index) == BINARY_RUNNING &&
		BIN_LOADVER(index) == bin->bin_ver && BIN_LOADINFO(index) == bin &&
		BIN_LOAD_ATTR(index).bin_size == bin->filelen &&
		BIN_LOAD_ATTR(index).offset == bin->offset &&
		BIN_NAME(index)[0] != '\0' && bin->sections[BIN_TEXT] != 0 &&
		bin->sizes[BIN_TEXT] != 0 && bin->sections[BIN_DATA] != 0;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: load_binary
 *
 * Description:
 *   This is a convenience function that wraps load_module and exec_module into
 *   one call.  If CONFIG_BINFMT_LOADABLE is defined, this function will
 *   schedule to unload the module when task exits.
 *
 * Input Parameters:
 *   filename  - The path to the program to be executed. If
 *               CONFIG_LIB_ENVPATH is defined in the configuration, then
 *               this may be a relative path from the current working
 *               directory. Otherwise, path must be the absolute path to the
 *               program.
 *
 *   load_attr - This structure contains the information that the binary
 *               to be loaded. load_attr will be NULL while loading a library.
 *
 * Returned Value:
 *   This is an end-user function, so it follows the normal convention:
 *   It returns the PID of the exec'ed module.  On failure, it returns
 *   -1 (ERROR) and sets errno appropriately.
 *
 ****************************************************************************/
#ifdef CONFIG_BINARY_MANAGER
int load_binary(int binary_idx, FAR const char *filename, load_attr_t *load_attr)
{
	FAR struct binary_s *bin = NULL;
	int pid;
	int errcode;
	int ret;
#if defined(CONFIG_SUPPORT_COMMON_BINARY) && \
	defined(CONFIG_APP_BINARY_SEPARATION) && defined(__KERNEL__)
	int cleanup_ret;
	bool common_domain_prepared = false;
	bool common_domain_active = false;
#endif

	/* Sanity check */
	if (!load_attr) {
		berr("ERROR: Invalid load_attr\n");
		errcode = -EINVAL;
		goto errout;
	} else if (load_attr->bin_size <= 0) {
		berr("ERROR: Invalid file length!\n");
		errcode = -EINVAL;
		goto errout;
	}

#ifdef CONFIG_OPTIMIZE_APP_RELOAD_TIME
	bin = load_attr->binp;

	/* If we find a non-null value for bin, it means that
	 * we are in a reload scenario.
	 */
	if (bin) {
		if (!bin->data_backup) {
			errcode = -EINVAL;
			berr("ERROR: Failed to find copy of data section from previous load\n");
			goto errout_with_bin;
		}

		memcpy((void *)bin->sections[BIN_DATA], (const void *)bin->data_backup, bin->sizes[BIN_DATA]);
		memset((void *)bin->sections[BIN_BSS], 0, bin->sizes[BIN_BSS]);
		bin->reload = false;
	} else {
#endif

		/* Allocate the load information */

		bin = (FAR struct binary_s *)kmm_zalloc(sizeof(struct binary_s));
		if (!bin) {
			berr("ERROR: Failed to allocate binary_s\n");
			errcode = -ENOMEM;
			goto errout_with_bin;
		}

		/* Initialize the binary structure */

		bin->filename = filename;

		if (load_attr) {
#ifdef CONFIG_SUPPORT_COMMON_BINARY
			if (binary_idx == BM_CMNLIB_IDX) {
				bin->islibrary = true;
				bin->binary_idx = binary_idx;
				bin->filelen = load_attr->bin_size;
				bin->offset = load_attr->offset;
				bin->bin_ver = load_attr->bin_ver;
#ifdef CONFIG_HAVE_CXX
				bin->run_library_ctors = true;
#endif
			} else
#endif
			{
				bin->exports = NULL;
				bin->nexports = 0;
				bin->filelen = load_attr->bin_size;
				bin->offset = load_attr->offset;
				bin->stacksize = load_attr->stack_size;
				bin->priority = load_attr->priority;
				bin->binary_idx = binary_idx;
				bin->bin_ver = load_attr->bin_ver;
#ifdef CONFIG_OPTIMIZE_APP_RELOAD_TIME
				strncpy(bin->bin_name, load_attr->bin_name, BIN_NAME_MAX);
#else
				bin->bin_name = load_attr->bin_name;
#endif
				bin->ramsize = load_attr->ram_size;
			}
		}

		/* Load the module into memory */
		ret = load_module(bin);
		if (ret < 0) {
			errcode = ret;
			berr("ERROR: Failed to load program '%s': %d\n", filename, errcode);
			goto errout_with_bin;
		}

#ifdef CONFIG_OPTIMIZE_APP_RELOAD_TIME
		if (!bin->data_backup) {
			errcode = -EINVAL;
			berr("ERROR: data section backup address not initialized\n");
			goto errout_with_bin;
		}

		memcpy((void *)bin->data_backup, (const void *)bin->sections[BIN_DATA], bin->sizes[BIN_DATA]);
	}
#endif

	/* Print Binary section address & size details */

	binfo("[%s] text    start addr =  0x%x  size = %u\n", bin->bin_name, bin->sections[BIN_TEXT], bin->sizes[BIN_TEXT]);
#ifdef CONFIG_OPTIMIZE_APP_RELOAD_TIME
	binfo("[%s] rodata  start addr =  0x%x  size = %u\n", bin->bin_name, bin->sections[BIN_RO], bin->sizes[BIN_RO]);
#endif
	binfo("[%s] data    start addr =  0x%x  size = %u\n", bin->bin_name, bin->sections[BIN_DATA], bin->sizes[BIN_DATA]);
	binfo("[%s] bss     start addr =  0x%x  size = %u\n", bin->bin_name, bin->sections[BIN_BSS], bin->sizes[BIN_BSS]);

	elf_save_bin_section_addr(bin);
	binfmt_arch_init_mem_protect(bin);

#ifdef CONFIG_SUPPORT_COMMON_BINARY
	if (bin->islibrary) {
		g_lib_binp = bin;
#if defined(CONFIG_APP_BINARY_SEPARATION) && defined(__KERNEL__)
		struct mm_loadable_domain_registration_s domain;
		uintptr_t writable_container;
		size_t writable_container_size;

		memset(&domain, 0, sizeof(domain));
#ifdef CONFIG_XIP_ELF
		writable_container = bin->ram_region_start;
		writable_container_size = bin->ram_region_end -
			bin->ram_region_start;
#elif defined(CONFIG_OPTIMIZE_APP_RELOAD_TIME) && \
	defined(CONFIG_BINFMT_SECTION_UNIFIED_MEMORY)
		writable_container = bin->ramstart;
		writable_container_size = bin->sizes[BIN_TEXT] +
			bin->sizes[BIN_RO] + bin->ramsize;
#elif defined(CONFIG_OPTIMIZE_APP_RELOAD_TIME)
		writable_container = bin->sections[BIN_DATA];
		writable_container_size = bin->ramsize;
#else
		writable_container = bin->ramstart;
		writable_container_size = bin->ramsize;
#endif
		domain.slot = binary_idx;
		domain.descriptor = bin;
		domain.descriptor_container = bin;
		domain.descriptor_container_size = sizeof(*bin);
		domain.name = filename;
		domain.ready = binfmt_common_domain_ready;
		domain.text_start = bin->sections[BIN_TEXT];
		domain.text_size = bin->sizes[BIN_TEXT];
		if (bin->sizes[BIN_DATA] != 0) {
			domain.writable[domain.writable_count].start = bin->sections[BIN_DATA];
			domain.writable[domain.writable_count].size = bin->sizes[BIN_DATA];
			domain.writable_count++;
		}
		if (bin->sizes[BIN_BSS] != 0) {
			domain.writable[domain.writable_count].start = bin->sections[BIN_BSS];
			domain.writable[domain.writable_count].size = bin->sizes[BIN_BSS];
			domain.writable_count++;
		}
		for (size_t index = 0; index < domain.writable_count; index++) {
			domain.writable[index].container = writable_container;
			domain.writable[index].container_size = writable_container_size;
		}
		ret = mm_loadable_domain_register(&domain);
		if (ret < 0) {
			errcode = ret;
			goto errout_with_unload;
		}
		common_domain_prepared = true;
#endif
		(void)binfmt_exchange_umm_app_id(
			(uint32_t *)(bin->sections[BIN_DATA] + 4));

		/* Update binary table */
		BIN_STATE(binary_idx) = BINARY_RUNNING;
		BIN_LOADVER(binary_idx) = bin->bin_ver;
		BIN_LOADINFO(binary_idx) = bin;
		BIN_LOAD_ATTR(binary_idx) = *load_attr;
		strncpy(BIN_NAME(binary_idx), load_attr->bin_name, BIN_NAME_MAX);
#if defined(CONFIG_APP_BINARY_SEPARATION) && defined(__KERNEL__)
		ret = mm_loadable_domain_activate(bin);
		if (ret < 0) {
			errcode = ret;
			goto errout_with_unload;
		}
		common_domain_active = true;
#endif
		return OK;
	}
	/* If we support common binary, then we need to place a pointer to the app's heap object
	 * into the heap table which is present at the start of the common library data section
	 */
	uint32_t *heap_table = (uint32_t *)(g_lib_binp->sections[BIN_DATA] + 8);
	heap_table[binary_idx] = bin->sections[BIN_HEAP];
#endif

	/* Start the module */
	pid = exec_module(bin);
	if (pid < 0) {
		errcode = pid;
		berr("ERROR: Failed to execute program '%s': %d\n", filename, errcode);
		elf_delete_bin_section_addr(bin->binary_idx);
		goto errout_with_unload;
	}

	return pid;

errout_with_unload:

#if defined(CONFIG_SUPPORT_COMMON_BINARY) && \
	defined(CONFIG_APP_BINARY_SEPARATION) && defined(__KERNEL__)
	if (common_domain_active) {
		cleanup_ret = mm_loadable_domain_disable_and_wait(bin);
		if (cleanup_ret == OK) {
			cleanup_ret = mm_loadable_domain_finish_unload(bin);
		}
	} else if (common_domain_prepared) {
		cleanup_ret = mm_loadable_domain_abort(bin);
	} else {
		cleanup_ret = OK;
	}
	if (cleanup_ret != OK) {
		berr("ERROR: common domain cleanup failed: %d\n", cleanup_ret);
		PANIC();
		set_errno(cleanup_ret);
		return ERROR;
	}
	if (bin != NULL && bin->islibrary) {
		(void)binfmt_exchange_umm_app_id(NULL);
		BIN_STATE(binary_idx) = BINARY_INACTIVE;
		BIN_LOADINFO(binary_idx) = NULL;
	}
#endif
	(void)unload_module(bin);
errout_with_bin:
	kmm_free(bin);
#ifdef CONFIG_SUPPORT_COMMON_BINARY
	g_lib_binp = NULL;
#endif
errout:
	set_errno(errcode);
	return ERROR;

}
#endif							/* CONFIG_BINARY_MANAGER */
#endif							/* CONFIG_BINFMT_ENABLE */
