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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <tinyara/uaccess.h>

#if !defined(CONFIG_BUILD_FLAT)
#include <tinyara/mm/mm.h>
#include <tinyara/sched.h>
#include <tinyara/userspace.h>

#if defined(CONFIG_APP_BINARY_SEPARATION) && defined(CONFIG_BINARY_MANAGER) && \
	(!defined(CONFIG_XIP_ELF) || defined(CONFIG_SUPPORT_COMMON_BINARY))
#include <tinyara/binfmt/binfmt.h>

#include "../binary_manager/binary_manager_internal.h"
#endif
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if !defined(CONFIG_BUILD_FLAT)
extern uint32_t _stext_flash;
extern uint32_t _etext_flash;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

#ifdef CONFIG_ARCH_HAVE_RAM_KERNEL_TEXT
extern uint32_t _stext_ram;
extern uint32_t _etext_ram;
#endif
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if !defined(CONFIG_BUILD_FLAT)
static bool uaccess_get_range(FAR const void *addr, unsigned long n,
							  uintptr_t *start, uintptr_t *end)
{
	uintptr_t first;
	uintptr_t len;

	if (n == 0) {
		*start = 0;
		*end = 0;
		return true;
	}

	if (addr == NULL) {
		return false;
	}

	first = (uintptr_t)addr;
	len = (uintptr_t)n;

	if ((unsigned long)len != n || len > UINTPTR_MAX - first) {
		return false;
	}

	*start = first;
	*end = first + len;
	return true;
}

static bool uaccess_ranges_overlap(uintptr_t start, uintptr_t end,
								   uintptr_t region_start,
								   uintptr_t region_end)
{
	return region_start < region_end && start < region_end &&
		   end > region_start;
}

static bool uaccess_range_inside(uintptr_t start, uintptr_t end,
								 uintptr_t region_start,
								 uintptr_t region_end)
{
	return region_start < region_end && start >= region_start &&
		   end <= region_end;
}

#if !defined(CONFIG_XIP_ELF) && defined(CONFIG_APP_BINARY_SEPARATION) && \
	defined(CONFIG_BINARY_MANAGER)
static bool uaccess_range_inside_sized_region(uintptr_t start, uintptr_t end,
											  uintptr_t region_start,
											  size_t region_size)
{
	uintptr_t size;

	if (region_start == 0 || region_size == 0) {
		return false;
	}

	size = (uintptr_t)region_size;
	if ((size_t)size != region_size || size > UINTPTR_MAX - region_start) {
		return false;
	}

	return uaccess_range_inside(start, end, region_start, region_start + size);
}

static bool uaccess_range_inside_binary_heap(FAR struct binary_s *binp,
											 uintptr_t start, uintptr_t end)
{
	uintptr_t heap_start;

	heap_start = (uintptr_t)binp->sections[BIN_HEAP];
	if (heap_start == 0 ||
		sizeof(struct mm_heap_s) > UINTPTR_MAX - heap_start) {
		return false;
	}

	return uaccess_range_inside_sized_region(start, end,
											heap_start + sizeof(struct mm_heap_s),
											binp->sizes[BIN_HEAP]);
}

static bool uaccess_range_inside_binary_data(FAR struct binary_s *binp,
											 uintptr_t start, uintptr_t end)
{
	uintptr_t data_start;
	uintptr_t heap_start;

	data_start = (uintptr_t)binp->sections[BIN_DATA];
	heap_start = (uintptr_t)binp->sections[BIN_HEAP];
	if (data_start == 0 || heap_start == 0) {
		return false;
	}

	return uaccess_range_inside(start, end, data_start, heap_start);
}

static bool uaccess_range_inside_binary_text(FAR struct binary_s *binp,
											 uintptr_t start, uintptr_t end)
{
#ifdef CONFIG_OPTIMIZE_APP_RELOAD_TIME
	if (uaccess_range_inside_sized_region(start, end,
										  (uintptr_t)binp->sections[BIN_TEXT],
										  binp->sizes[BIN_TEXT]) ||
		uaccess_range_inside_sized_region(start, end,
										  (uintptr_t)binp->sections[BIN_RO],
										  binp->sizes[BIN_RO])) {
		return true;
	}

	return false;
#else
	if (binp->ramstart == 0 || binp->sections[BIN_HEAP] == 0) {
		return false;
	}

	return uaccess_range_inside(start, end, (uintptr_t)binp->ramstart,
								(uintptr_t)binp->sections[BIN_HEAP]);
#endif
}

static FAR struct binary_s *uaccess_current_binary(void)
{
	FAR struct tcb_s *tcb;
	int bin_idx;

	tcb = sched_self();
	if (tcb == NULL || tcb->group == NULL) {
		return NULL;
	}

	bin_idx = tcb->group->tg_binidx;
	if (bin_idx <= 0 || bin_idx > CONFIG_NUM_APPS) {
		return NULL;
	}

	return BIN_LOADINFO(bin_idx);
}

static bool uaccess_range_inside_binary(FAR struct binary_s *binp,
										uintptr_t start, uintptr_t end,
										bool write)
{
	if (binp == NULL) {
		return false;
	}

	if (uaccess_range_inside_binary_data(binp, start, end)) {
		return true;
	}

	if (uaccess_range_inside_binary_heap(binp, start, end)) {
		return true;
	}

	if (!write && uaccess_range_inside_binary_text(binp, start, end)) {
		return true;
	}

	return false;
}

static bool uaccess_range_inside_userspace(uintptr_t start, uintptr_t end,
										   bool write)
{
	FAR struct binary_s *binp;

	binp = uaccess_current_binary();
	if (uaccess_range_inside_binary(binp, start, end, write)) {
		return true;
	}

#ifdef CONFIG_SUPPORT_COMMON_BINARY
	binp = BIN_LOADINFO(BM_CMNLIB_IDX);
	if (binp != NULL && binp->islibrary &&
		uaccess_range_inside_binary(binp, start, end, write)) {
		return true;
	}
#endif

	return false;
}
#elif defined(CONFIG_XIP_ELF) && defined(CONFIG_APP_BINARY_SEPARATION) && \
	defined(CONFIG_BUILD_PROTECTED)
#if defined(CONFIG_SUPPORT_COMMON_BINARY) && defined(CONFIG_BINARY_MANAGER)
static bool uaccess_range_inside_common_binary(uintptr_t start, uintptr_t end,
											   bool write)
{
	FAR struct binary_s *binp;

	binp = BIN_LOADINFO(BM_CMNLIB_IDX);
	if (binp == NULL || !binp->islibrary) {
		return false;
	}

	if (uaccess_range_inside(start, end, (uintptr_t)binp->ram_region_start,
							 (uintptr_t)binp->ram_region_end)) {
		return true;
	}

	if (!write &&
		uaccess_range_inside(start, end, (uintptr_t)binp->flash_region_start,
							 (uintptr_t)binp->flash_region_end)) {
		return true;
	}

	return false;
}
#endif

static bool uaccess_range_inside_userspace(uintptr_t start, uintptr_t end,
										   bool write)
{
	FAR struct tcb_s *tcb;
	FAR struct userspace_s *uspace;

	tcb = sched_self();
	if (tcb == NULL) {
		return false;
	}

	uspace = (FAR struct userspace_s *)tcb->uspace;
	if (uspace == NULL) {
		return false;
	}

	if (uaccess_range_inside(start, end, (uintptr_t)uspace->ram_start,
							 (uintptr_t)uspace->ram_end)) {
		return true;
	}

	if (!write &&
		uaccess_range_inside(start, end, (uintptr_t)uspace->text_start,
							 (uintptr_t)uspace->flash_end)) {
		return true;
	}

#if defined(CONFIG_SUPPORT_COMMON_BINARY) && defined(CONFIG_BINARY_MANAGER)
	if (uaccess_range_inside_common_binary(start, end, write)) {
		return true;
	}
#endif

	return false;
}
#else
static bool uaccess_range_inside_userspace(uintptr_t start, uintptr_t end,
										   bool write)
{
	return false;
}
#endif

static bool uaccess_range_intersects_kernel_space(uintptr_t start,
												  uintptr_t end)
{
	if (start == end) {
		return false;
	}

	if (uaccess_ranges_overlap(start, end, (uintptr_t)&_stext_flash,
							   (uintptr_t)&_etext_flash)) {
		return true;
	}

#ifdef CONFIG_ARCH_HAVE_RAM_KERNEL_TEXT
	if (uaccess_ranges_overlap(start, end, (uintptr_t)&_stext_ram,
							   (uintptr_t)&_etext_ram)) {
		return true;
	}
#endif

	if (uaccess_ranges_overlap(start, end, (uintptr_t)&_sdata,
							   (uintptr_t)&_edata) ||
		uaccess_ranges_overlap(start, end, (uintptr_t)&_sbss,
							   (uintptr_t)&_ebss)) {
		return true;
	}

	return false;
}

static bool uaccess_range_is_user_accessible(uintptr_t start, uintptr_t end,
											 bool write)
{
	if (start == end) {
		return true;
	}

	return uaccess_range_inside_userspace(start, end, write);
}

static bool uaccess_user_range_invalid(FAR const void *addr, unsigned long n,
									   bool write)
{
	uintptr_t start;
	uintptr_t end;

	if (!uaccess_get_range(addr, n, &start, &end)) {
		return true;
	}

	if (uaccess_range_intersects_kernel_space(start, end)) {
		return true;
	}

	return !uaccess_range_is_user_accessible(start, end, write);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

unsigned long copy_from_user(FAR void *to, FAR const void __user *from,
							 unsigned long n)
{
	if (n == 0) {
		return 0;
	}

	if (to == NULL) {
		return n;
	}

	if (from == NULL) {
		memset(to, 0, n);
		return n;
	}

#if !defined(CONFIG_BUILD_FLAT)
	if (uaccess_user_range_invalid(from, n, false)) {
		memset(to, 0, n);
		return n;
	}
#endif

	memcpy(to, from, n);
	return 0;
}

unsigned long copy_to_user(FAR void __user *to, FAR const void *from,
						   unsigned long n)
{
	if (n == 0) {
		return 0;
	}

	if (from == NULL) {
		return n;
	}

	if (to == NULL) {
		return n;
	}

#if !defined(CONFIG_BUILD_FLAT)
	if (uaccess_user_range_invalid(to, n, true)) {
		return n;
	}
#endif

	memcpy(to, from, n);
	return 0;
}
