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

#include <assert.h>
#include <debug.h>
#include <stdint.h>

#include <tinyara/mm/mm.h>

#include "mm_smallpool.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if !defined(CONFIG_MM_SMALL_ALLOC_POOL_16) && !defined(CONFIG_MM_SMALL_ALLOC_POOL_32) && !defined(CONFIG_MM_SMALL_ALLOC_POOL_64)
#error "CONFIG_MM_SMALL_ALLOC_POOL requires at least one enabled class"
#endif

#if (CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE & (CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE - 1)) != 0
#error "CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE must be a power of two"
#endif

#if CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE < MM_MIN_CHUNK
#error "CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE must be at least MM_MIN_CHUNK"
#endif

#if defined(CONFIG_MM_SMALL_ALLOC_POOL_64)
#define MM_SMALLPOOL_MAX_SLOT_SIZE 64
#elif defined(CONFIG_MM_SMALL_ALLOC_POOL_32)
#define MM_SMALLPOOL_MAX_SLOT_SIZE 32
#else
#define MM_SMALLPOOL_MAX_SLOT_SIZE 16
#endif

#if CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE < MM_SMALLPOOL_MAX_SLOT_SIZE
#error "CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE must fit the largest enabled slot"
#endif

#define MM_SMALLPOOL_MAGIC 0x534c4142
#define MM_SMALLPOOL_STATIC_ASSERT(name, cond) typedef char name[(cond) ? 1 : -1]

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mm_smallpool_slot_s {
	FAR struct mm_smallpool_slot_s *flink;
};

struct mm_smallpool_slab_s {
	uint32_t magic;
	uint16_t slot_size;
	uint16_t total_slots;
	uint16_t free_slots;
	uint16_t reserved;
	FAR struct mm_smallpool_slab_s *flink;
	FAR struct mm_smallpool_slot_s *free;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

MM_SMALLPOOL_STATIC_ASSERT(mm_smallpool_chunk_capacity_check,
	(sizeof(struct mm_smallpool_slab_s) + MM_SMALLPOOL_MAX_SLOT_SIZE) <= CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE);

#ifdef CONFIG_MM_SMALL_ALLOC_POOL_16
MM_SMALLPOOL_STATIC_ASSERT(mm_smallpool_delaynode_16_check,
	sizeof(struct mm_delaynode_s) <= 16);
#endif

#ifdef CONFIG_MM_SMALL_ALLOC_POOL_32
MM_SMALLPOOL_STATIC_ASSERT(mm_smallpool_delaynode_32_check,
	sizeof(struct mm_delaynode_s) <= 32);
#endif

#ifdef CONFIG_MM_SMALL_ALLOC_POOL_64
MM_SMALLPOOL_STATIC_ASSERT(mm_smallpool_delaynode_64_check,
	sizeof(struct mm_delaynode_s) <= 64);
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uintptr_t mm_smallpool_align_up(uintptr_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static size_t mm_smallpool_choose_slot_size(size_t size)
{
	if (size == 0) {
		return 0;
	}

#ifdef CONFIG_MM_SMALL_ALLOC_POOL_16
	if (size <= 16) {
		return 16;
	}
#endif

#ifdef CONFIG_MM_SMALL_ALLOC_POOL_32
	if (size <= 32) {
		return 32;
	}
#endif

#ifdef CONFIG_MM_SMALL_ALLOC_POOL_64
	if (size <= 64) {
		return 64;
	}
#endif

	return 0;
}

static bool mm_smallpool_valid_slot_size(size_t slot_size)
{
	switch (slot_size) {
#ifdef CONFIG_MM_SMALL_ALLOC_POOL_16
	case 16:
		return true;
#endif
#ifdef CONFIG_MM_SMALL_ALLOC_POOL_32
	case 32:
		return true;
#endif
#ifdef CONFIG_MM_SMALL_ALLOC_POOL_64
	case 64:
		return true;
#endif
	default:
		return false;
	}
}

static FAR struct mm_smallpool_slab_s **mm_smallpool_slab_list(FAR struct mm_heap_s *heap, size_t slot_size)
{
	switch (slot_size) {
	case 16:
		return &heap->mm_smallpool_slab16;
	case 32:
		return &heap->mm_smallpool_slab32;
	case 64:
		return &heap->mm_smallpool_slab64;
	default:
		return NULL;
	}
}

static bool mm_smallpool_slab_in_heap(FAR struct mm_heap_s *heap, FAR struct mm_smallpool_slab_s *slab)
{
	uintptr_t start;
	uintptr_t end;
	uintptr_t region_start;
	uintptr_t region_end;
	int region;
	int nregions = 1;

	start = (uintptr_t)slab;
	end = start + CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE;
	if (end < start) {
		return false;
	}

#if CONFIG_KMM_REGIONS > 1
	nregions = heap->mm_nregions;
#endif

	for (region = 0; region < nregions; region++) {
		region_start = (uintptr_t)heap->mm_heapstart[region];
		region_end = (uintptr_t)heap->mm_heapend[region];
		if (start >= region_start && end <= region_end) {
			return true;
		}
	}

	return false;
}

static FAR struct mm_smallpool_slab_s *mm_smallpool_find_free_slab(FAR struct mm_heap_s *heap, size_t slot_size)
{
	FAR struct mm_smallpool_slab_s **list;
	FAR struct mm_smallpool_slab_s *slab;

	list = mm_smallpool_slab_list(heap, slot_size);
	if (!list) {
		return NULL;
	}

	for (slab = *list; slab; slab = slab->flink) {
		if (slab->free) {
			return slab;
		}
	}

	return NULL;
}

static void mm_smallpool_build_slots(FAR struct mm_smallpool_slab_s *slab, size_t slot_size)
{
	uintptr_t slot;
	uintptr_t end;

	slot = mm_smallpool_align_up((uintptr_t)slab + sizeof(struct mm_smallpool_slab_s), slot_size);
	end = (uintptr_t)slab + CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE;

	slab->free = NULL;
	slab->total_slots = 0;
	slab->free_slots = 0;

	while (slot + slot_size <= end) {
		FAR struct mm_smallpool_slot_s *node = (FAR struct mm_smallpool_slot_s *)slot;

		node->flink = slab->free;
		slab->free = node;
		slab->total_slots++;
		slab->free_slots++;
		slot += slot_size;
	}
}

static void mm_smallpool_add_slab(FAR struct mm_heap_s *heap, FAR struct mm_smallpool_slab_s *slab, size_t slot_size)
{
	FAR struct mm_smallpool_slab_s **list;

	list = mm_smallpool_slab_list(heap, slot_size);
	DEBUGASSERT(list);

	slab->magic = MM_SMALLPOOL_MAGIC;
	slab->slot_size = slot_size;
	slab->reserved = 0;
	mm_smallpool_build_slots(slab, slot_size);
	slab->flink = *list;
	*list = slab;
}

static FAR void *mm_smallpool_pop_slot(FAR struct mm_smallpool_slab_s *slab)
{
	FAR struct mm_smallpool_slot_s *slot;

	slot = slab->free;
	if (!slot) {
		return NULL;
	}

	slab->free = slot->flink;
	slab->free_slots--;
	return slot;
}

static bool mm_smallpool_slab_contains(FAR struct mm_smallpool_slab_s *slab, FAR void *mem)
{
	uintptr_t start;
	uintptr_t end;
	uintptr_t addr;

	start = mm_smallpool_align_up((uintptr_t)slab + sizeof(struct mm_smallpool_slab_s), slab->slot_size);
	end = (uintptr_t)slab + CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE;
	addr = (uintptr_t)mem;

	return addr >= start && addr < end && ((addr - start) % slab->slot_size) == 0;
}

static bool mm_smallpool_slab_is_linked(FAR struct mm_heap_s *heap, FAR struct mm_smallpool_slab_s *slab)
{
	FAR struct mm_smallpool_slab_s **list;
	FAR struct mm_smallpool_slab_s *node;

	list = mm_smallpool_slab_list(heap, slab->slot_size);
	if (!list) {
		return false;
	}

	for (node = *list; node; node = node->flink) {
		if (node == slab) {
			return true;
		}
	}

	return false;
}

static bool mm_smallpool_slot_is_free(FAR struct mm_smallpool_slab_s *slab, FAR struct mm_smallpool_slot_s *slot)
{
	FAR struct mm_smallpool_slot_s *node;

	for (node = slab->free; node; node = node->flink) {
		if (node == slot) {
			return true;
		}
	}

	return false;
}

static FAR struct mm_smallpool_slab_s *mm_smallpool_find_slab_locked(FAR struct mm_heap_s *heap, FAR void *mem)
{
	FAR struct mm_smallpool_slab_s *slab;

	if (!heap || !heap->mm_smallpool_enabled || !mem) {
		return NULL;
	}

	slab = (FAR struct mm_smallpool_slab_s *)((uintptr_t)mem & ~(CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE - 1));
	if (!mm_smallpool_slab_in_heap(heap, slab)) {
		return NULL;
	}

	if (slab->magic != MM_SMALLPOOL_MAGIC) {
		return NULL;
	}

	if (!mm_smallpool_valid_slot_size(slab->slot_size)) {
		return NULL;
	}

	if (!mm_smallpool_slab_contains(slab, mem)) {
		return NULL;
	}

	if (!mm_smallpool_slab_is_linked(heap, slab)) {
		return NULL;
	}

	return slab;
}

static void mm_smallpool_detach_slab(FAR struct mm_heap_s *heap, FAR struct mm_smallpool_slab_s *slab)
{
	FAR struct mm_smallpool_slab_s **list;
	FAR struct mm_smallpool_slab_s *node;

	list = mm_smallpool_slab_list(heap, slab->slot_size);
	DEBUGASSERT(list);

	if (*list == slab) {
		*list = slab->flink;
		slab->flink = NULL;
		return;
	}

	for (node = *list; node && node->flink != slab; node = node->flink);
	if (node) {
		node->flink = slab->flink;
		slab->flink = NULL;
	}
}

static void mm_smallpool_free_slab_list(FAR struct mm_heap_s *heap, FAR struct mm_smallpool_slab_s *slab)
{
	while (slab) {
		FAR struct mm_smallpool_slab_s *next = slab->flink;

		slab->magic = 0;
		slab->flink = NULL;
		mm_free(heap, slab);
		slab = next;
	}
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void mm_smallpool_initialize(FAR struct mm_heap_s *heap)
{
	heap->mm_smallpool_enabled = false;
	heap->mm_smallpool_slab16 = NULL;
	heap->mm_smallpool_slab32 = NULL;
	heap->mm_smallpool_slab64 = NULL;
}

void mm_smallpool_enable(FAR struct mm_heap_s *heap)
{
	if (mm_takesemaphore(heap) == false) {
		return;
	}

	heap->mm_smallpool_enabled = true;
	mm_givesemaphore(heap);
}

void mm_smallpool_disable(FAR struct mm_heap_s *heap)
{
	FAR struct mm_smallpool_slab_s *slab16;
	FAR struct mm_smallpool_slab_s *slab32;
	FAR struct mm_smallpool_slab_s *slab64;

	if (mm_takesemaphore(heap) == false) {
		return;
	}

	heap->mm_smallpool_enabled = false;
	slab16 = heap->mm_smallpool_slab16;
	slab32 = heap->mm_smallpool_slab32;
	slab64 = heap->mm_smallpool_slab64;
	heap->mm_smallpool_slab16 = NULL;
	heap->mm_smallpool_slab32 = NULL;
	heap->mm_smallpool_slab64 = NULL;
	mm_givesemaphore(heap);

	mm_smallpool_free_slab_list(heap, slab16);
	mm_smallpool_free_slab_list(heap, slab32);
	mm_smallpool_free_slab_list(heap, slab64);
}

FAR void *mm_smallpool_malloc(FAR struct mm_heap_s *heap, size_t size, mmaddress_t caller_retaddr)
{
	FAR struct mm_smallpool_slab_s *slab;
	FAR void *mem;
	size_t slot_size;

	slot_size = mm_smallpool_choose_slot_size(size);
	if (!slot_size) {
		return NULL;
	}

	if (mm_takesemaphore(heap) == false) {
		return NULL;
	}

	if (!heap->mm_smallpool_enabled) {
		mm_givesemaphore(heap);
		return NULL;
	}

	slab = mm_smallpool_find_free_slab(heap, slot_size);
	if (slab) {
		mem = mm_smallpool_pop_slot(slab);
		mm_givesemaphore(heap);
		return mem;
	}

	mm_givesemaphore(heap);

	slab = mm_memalign(heap, CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE, CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE, caller_retaddr);
	if (!slab) {
		return NULL;
	}

	if (mm_takesemaphore(heap) == false) {
		mm_free(heap, slab);
		return NULL;
	}

	if (!heap->mm_smallpool_enabled) {
		mm_givesemaphore(heap);
		mm_free(heap, slab);
		return NULL;
	}

	mm_smallpool_add_slab(heap, slab, slot_size);
	mem = mm_smallpool_pop_slot(slab);
	mm_givesemaphore(heap);
	return mem;
}

bool mm_smallpool_free_locked(FAR struct mm_heap_s *heap, FAR void *mem, mmaddress_t free_call_addr, pid_t free_call_pid, FAR void **slab_to_free)
{
	FAR struct mm_smallpool_slab_s *slab;
	FAR struct mm_smallpool_slot_s *slot;

	if (slab_to_free) {
		*slab_to_free = NULL;
	}

	slab = mm_smallpool_find_slab_locked(heap, mem);
	if (!slab) {
		return false;
	}

	slot = (FAR struct mm_smallpool_slot_s *)mem;
	if (mm_smallpool_slot_is_free(slab, slot)) {
		mdbg("WARNING!! Attempt for double freeing a small pool pointer by pid %d at address 0x%08x\n",
			free_call_pid, free_call_addr);
		return true;
	}

	slot->flink = slab->free;
	slab->free = slot;
	slab->free_slots++;

	if (slab->free_slots == slab->total_slots) {
		mm_smallpool_detach_slab(heap, slab);
		slab->magic = 0;
		if (slab_to_free) {
			*slab_to_free = slab;
		}
	}

	return true;
}

size_t mm_smallpool_get_class_size(size_t size)
{
	return mm_smallpool_choose_slot_size(size);
}

bool mm_smallpool_get_slot_size(FAR struct mm_heap_s *heap, FAR void *mem, FAR size_t *slot_size)
{
	FAR struct mm_smallpool_slab_s *slab;

	if (slot_size) {
		*slot_size = 0;
	}

	if (mm_takesemaphore(heap) == false) {
		return false;
	}

	slab = mm_smallpool_find_slab_locked(heap, mem);
	if (!slab) {
		mm_givesemaphore(heap);
		return false;
	}

	if (slot_size) {
		*slot_size = slab->slot_size;
	}

	mm_givesemaphore(heap);
	return true;
}
