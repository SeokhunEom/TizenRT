/****************************************************************************
 *
 * Copyright 2023 Samsung Electronics All Rights Reserved.
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
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <tinyara/sched.h>
#include <tinyara/mm/mm.h>
#include "bridge/nuttx_heap_bridge.h"
#include <tinyara/security_level.h>

/************************************************************************
 * Name: heap_dbg
 *
 * Description: Dump heap info debug logs without adding function name to start of log.
 ************************************************************************/
int heap_dbg(const char *fmt, ...)
{
	va_list ap;
	int ret;

	if (IS_SECURE_STATE()) {
		return 0;
	}

	va_start(ap, fmt);
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
	extern bool abort_mode;

	if (abort_mode) {
		ret = lowvsyslog(LOG_ERR, fmt, ap);
	} else {
		ret = vsyslog(LOG_ERR, fmt, ap);
	}
#else
	ret = vsyslog(LOG_ERR, fmt, ap);
#endif
	va_end(ap);

	return ret;
}

/************************************************************************
 * Name: mm_dump_heap_region
 *
 * Description: Print the hex value contents of heap from start to end address.
 ************************************************************************/
void mm_dump_heap_region(uint32_t start, uint32_t end)
{
	heap_dbg("#########################################################################################\n");
	heap_dbg("Dump heap: 0x%08x - 0x%08x\n", start, end);
	heap_dbg("#########################################################################################\n");
	for (; start < end; start += 32) {
		uint32_t *ptr = (uint32_t *)start;
		heap_dbg("%08x: %08x %08x %08x %08x %08x %08x %08x %08x\n",
			   start, ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7]);
	}
	heap_dbg("#########################################################################################\n");
}

/************************************************************************
 * Name: mm_dump_heap_free_node_list
 *
 * Description: Print the hex value contents of heap free nodes.
 ************************************************************************/
static void dump_free(const struct trmm_node *node, void *arg)
{
    (void)arg;
    if (!node->allocated) {
        heap_dbg("free %p size=%lu\n", node->address, (unsigned long)node->size);
    }
}

void mm_dump_heap_free_node_list(struct mm_heap_s *heap)
{
    trmm_walk(heap->nuttx_heap, dump_free, NULL);
}

void mm_dump_node(struct mm_allocnode_s *node, char *node_type)
{
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	char is_stack[9] = "'s stack";
#if CONFIG_TASK_NAME_SIZE > 0
	char myname[CONFIG_TASK_NAME_SIZE + 1];
#endif
#endif

	if (!node) {
		mfdbg("Unable to dump NULL node\n");
		return;
	}

	mfdbg("%s: addr = 0x%08x, type = %c, size = %u, preceding size = %u\n", node_type, node, node->preceding & MM_ALLOC_BIT ? 'A' : 'F', node->size, node->preceding & ~MM_ALLOC_BIT);

#ifdef CONFIG_DEBUG_MM_HEAPINFO
	pid_t pid = node->pid;
	if (pid < 0) {
		/* For stack allocated node, pid is negative value.
		 * To use the pid, change it to original positive value.
		 */
		pid = (-1) * pid;
	} else {
		is_stack[0] = '\0';
	}
#if CONFIG_TASK_NAME_SIZE > 0
	if (prctl(PR_GET_NAME_BYPID, myname, pid) == OK) {
		mfdbg("%s owner pid = %d (%s%s), allocated by code at addr = 0x%08x\n", node_type, node->pid, myname, is_stack, node->alloc_call_addr);
	} else {
		mfdbg("%s owner pid = %d (Exited Task%s), allocated by code at addr = 0x%08x\n", node_type, node->pid, is_stack, node->alloc_call_addr);
	}
#else
	mfdbg("%s owner pid = %d%s, allocated by code at addr = 0x%08x\n", node_type, node->pid, is_stack, node->alloc_call_addr);
#endif
#endif
}

#ifdef CONFIG_BUILD_PROTECTED
#define MM_PUBLIC_HEAP_COUNT 1
#else
#define MM_PUBLIC_HEAP_COUNT CONFIG_KMM_NHEAPS
#endif

size_t mm_get_largest_freenode_size(void)
{
    size_t largest = 0;
    int i;
    struct mallinfo info;
    for (i = 0; i < MM_PUBLIC_HEAP_COUNT; i++) {
        mm_mallinfo(&BASE_HEAP[i], &info);
        if (info.mxordblk > largest) { largest = info.mxordblk; }
    }
    return largest;
}

#ifdef CONFIG_DEBUG_MM_HEAPINFO
size_t mm_get_heap_free_size(void)
{
    size_t total = 0;
    int i;
    struct mallinfo info;
    for (i = 0; i < MM_PUBLIC_HEAP_COUNT; i++) {
        mm_mallinfo(&BASE_HEAP[i], &info);
        total += info.fordblks;
    }
    return total;
}

void heapinfo_parse_heap(struct mm_heap_s *heap, int mode, pid_t pid)
{
    struct mallinfo info;
    struct mm_allocnode_s *node;
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
    if (abort_mode || up_interrupt_context()) {
        heap_dbg("Heap statistics unavailable in fault/IRQ context\n");
        return;
    }
#endif
    if (mm_trysemaphore(heap) != OK) {
        heap_dbg("Heap statistics unavailable (metadata busy)\n");
        return;
    }
    if (mm_mallinfo(heap, &info) != OK) {
        heap_dbg("Heap statistics unavailable (backend busy)\n");
        mm_givesemaphore(heap);
        return;
    }
    heap_dbg("NuttX %s heap: arena=%lu used=%lu free=%lu largest=%lu peak=%lu\n",
        mm_nuttx_backend_version(), (unsigned long)info.arena,
        (unsigned long)info.uordblks, (unsigned long)info.fordblks,
        (unsigned long)info.mxordblk, (unsigned long)heap->peak_alloc_size);
    if (mode == HEAPINFO_INIT_PEAK) { heapinfo_peak_init(heap); }
    else if (mode == HEAPINFO_DUMP_HEAP) { heapinfo_dump_heap(heap); }
    else if (mode == HEAPINFO_DETAIL_FREE) { mm_dump_heap_free_node_list(heap); }
    else if (mode != HEAPINFO_SIMPLE) {
        heap_dbg("Address      Size     PID Caller\n");
        for (node = mm_first_allocnode(heap); node; node = mm_next_allocnode(node)) {
            if (pid == HEAPINFO_PID_ALL || node->pid == pid || node->pid == -pid) {
                heap_dbg("%p %8lu %6d %p\n", (char *)node + SIZEOF_MM_ALLOCNODE,
                    (unsigned long)(node->size - SIZEOF_MM_ALLOCNODE),
                    node->pid, node->alloc_call_addr);
            }
        }
    }
    mm_givesemaphore(heap);
}
#endif
