/****************************************************************************
 *
 * Copyright 2022 Samsung Electronics All Rights Reserved.
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

/************************************************************************
 * Included Files
 ************************************************************************/

#include <tinyara/config.h>
#include <tinyara/mm/mm.h>

/* This is the kernel heap */

struct mm_heap_s g_kmmheap[CONFIG_KMM_NHEAPS];

/************************************************************************
 * Public Functions
 ************************************************************************/
struct mm_heap_s *kmm_get_baseheap(void)
{
	return g_kmmheap;
}

/************************************************************************
 * Name: kmm_get_heap
 *
 * Description:
 *   Returns the kernel heap structure.
 *
 ************************************************************************/
struct mm_heap_s *kmm_get_heap(void *addr)
{
	return mm_get_heap(addr);
}

/************************************************************************
 * Name: kmm_get_heap_with_index
 *
 * Description:
 *   Returns the kernel heap matched with index.
 *
 ************************************************************************/
struct mm_heap_s *kmm_get_heap_with_index(int index)
{
	if (index >= CONFIG_KMM_NHEAPS) {
		mdbg("heap index is out of range.\n");
		return NULL;
	}
	return &g_kmmheap[index];
}

/************************************************************************
 * Name: kmm_get_index_of_heap
 *
 * Description:
 *   Returns the kernel heap index.
 *
 ************************************************************************/
int kmm_get_index_of_heap(void *mem)
{
	int heap_idx;

	if (mem == NULL) {
		return INVALID_HEAP_IDX;
	}

	/* Search the kernel heaps (g_kmmheap) directly.  We must not delegate to
	 * mm_get_index_of_heap() here: in the protected/kernel build BASE_HEAP
	 * resolves to the current task's user heap, so a kernel-heap address would
	 * never be found and it would wrongly return INVALID_HEAP_IDX.
	 */
	for (heap_idx = 0; heap_idx < CONFIG_KMM_NHEAPS; heap_idx++) {
		int region = 0;
#if CONFIG_KMM_REGIONS > 1
		for (; region < g_kmmheap[heap_idx].mm_nregions; region++)
#endif
		{
			if ((mem > (void *)g_kmmheap[heap_idx].mm_heapstart[region]) && (mem < (void *)g_kmmheap[heap_idx].mm_heapend[region])) {
				return heap_idx;
			}
		}
	}

	return INVALID_HEAP_IDX;
}
