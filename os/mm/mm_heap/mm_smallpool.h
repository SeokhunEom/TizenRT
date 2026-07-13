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

#ifndef __MM_MM_HEAP_MM_SMALLPOOL_H
#define __MM_MM_HEAP_MM_SMALLPOOL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <tinyara/mm/mm.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_MM_SMALL_ALLOC_POOL
void mm_smallpool_initialize(FAR struct mm_heap_s *heap);
void mm_smallpool_enable(FAR struct mm_heap_s *heap);
void mm_smallpool_disable(FAR struct mm_heap_s *heap);
FAR void *mm_smallpool_malloc(FAR struct mm_heap_s *heap, size_t size, mmaddress_t caller_retaddr);
bool mm_smallpool_free_locked(FAR struct mm_heap_s *heap, FAR void *mem, mmaddress_t free_call_addr, pid_t free_call_pid, FAR void **slab_to_free);
size_t mm_smallpool_get_class_size(size_t size);
bool mm_smallpool_get_slot_size(FAR struct mm_heap_s *heap, FAR void *mem, FAR size_t *slot_size);
#else
static inline void mm_smallpool_initialize(FAR struct mm_heap_s *heap)
{
	(void)heap;
}

static inline void mm_smallpool_enable(FAR struct mm_heap_s *heap)
{
	(void)heap;
}

static inline void mm_smallpool_disable(FAR struct mm_heap_s *heap)
{
	(void)heap;
}

static inline FAR void *mm_smallpool_malloc(FAR struct mm_heap_s *heap, size_t size, mmaddress_t caller_retaddr)
{
	(void)heap;
	(void)size;
	(void)caller_retaddr;
	return NULL;
}

static inline bool mm_smallpool_free_locked(FAR struct mm_heap_s *heap, FAR void *mem, mmaddress_t free_call_addr, pid_t free_call_pid, FAR void **slab_to_free)
{
	(void)heap;
	(void)mem;
	(void)free_call_addr;
	(void)free_call_pid;
	if (slab_to_free) {
		*slab_to_free = NULL;
	}

	return false;
}

static inline size_t mm_smallpool_get_class_size(size_t size)
{
	(void)size;
	return 0;
}

static inline bool mm_smallpool_get_slot_size(FAR struct mm_heap_s *heap, FAR void *mem, FAR size_t *slot_size)
{
	(void)heap;
	(void)mem;
	if (slot_size) {
		*slot_size = 0;
	}

	return false;
}
#endif

#endif /* __MM_MM_HEAP_MM_SMALLPOOL_H */
