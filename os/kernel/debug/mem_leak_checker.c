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
#include <debug.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <sched.h>
#include <tinyara/irq.h>
#include <string.h>
#include <queue.h>
#include <sys/types.h>
#include <tinyara/mm/mm.h>
#include <tinyara/mm/heap_regioninfo.h>
#include <arch/chip/memory_region.h>
#include <tinyara/binfmt/elf.h>

#include "binary_manager/binary_manager_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define CMN_BIN_IDX 0

#define MAX_ALLOC_COUNT    CONFIG_MEM_LEAK_CHECKER_MAX_ALLOC_COUNT
#define HASH_SIZE          CONFIG_MEM_LEAK_CHECKER_HASH_TABLE_SIZE
#define MEM_DUMP_MAX_BYTES 32

/* Node addresses are hash identities only. All report data is copied while
 * heap locks protect the allocations, so printing never touches freed nodes.
 */
struct alloc_node_info_s {
	uintptr_t node;
	struct alloc_node_info_s *next;
	void *data;
	size_t size;
	mmaddress_t caller;
	pid_t pid;
	bool used;
	unsigned char sample[MEM_DUMP_MAX_BYTES];
};

static struct alloc_node_info_s **g_hash_table;
static struct alloc_node_info_s *g_node_info;
static bool g_checker_running;

static bool claim_checker(void)
{
	irqstate_t flags = enter_critical_section();
	bool available = !g_checker_running;
	if (available) { g_checker_running = true; }
	leave_critical_section(flags);
	return available;
}

static void release_checker(void)
{
	irqstate_t flags = enter_critical_section();
	g_checker_running = false;
	leave_critical_section(flags);
}

static int hash_init(void)
{
	int index;

	g_hash_table = (struct alloc_node_info_s **)malloc(sizeof(struct alloc_node_info_s *) * HASH_SIZE);
	if (!g_hash_table) {
		return ERROR;
	}

	g_node_info = (struct alloc_node_info_s*)malloc(sizeof(struct alloc_node_info_s) * MAX_ALLOC_COUNT);
	if (!g_node_info) {
		free(g_hash_table);
		g_hash_table = NULL;
		return ERROR;
	}

	for (index = 0; index < HASH_SIZE; ++index) {
		g_hash_table[index] = NULL;
	}

	return OK;
}

static void hash_deinit(void)
{
	free(g_hash_table);
	free(g_node_info);
	g_hash_table = NULL;
	g_node_info = NULL;
}

static uintptr_t hash_key(uintptr_t value)
{
	return value % (uintptr_t)HASH_SIZE;
}

static void add_hash(int index)
{
	uintptr_t key;
	struct alloc_node_info_s *cur;

	key = hash_key((uintptr_t)g_node_info[index].node);
	if (g_hash_table[key] == NULL) {
		g_hash_table[key] = &g_node_info[index];
		return;
	}

	cur = g_hash_table[key];
	while (cur->next) {
		cur = cur->next;
	}
	cur->next = &g_node_info[index];
}

static bool search_hash(uintptr_t value)
{
	uintptr_t key = hash_key(value);
	struct alloc_node_info_s *cur = g_hash_table[key];

	while (cur != NULL) {
		if ((uintptr_t)cur->node == value) {
			if (cur->used) {
				return false;
			}
			cur->used = true;
			return true;
		}
		if (cur->next == NULL) {
			return false;
		}
		cur = cur->next;
	}
	return false;
}

/* The caller owns every heap lock used by the scan. No allocation or I/O
 * is permitted between this snapshot and the last payload read.
 */
static int fill_hash_table(struct mm_heap_s *heap, int *node_count)
{
	struct mm_allocnode_s *node;
	int count = *node_count;
	if (mm_check_heap_corruption(heap) != OK) { return ERROR; }
	for (node = mm_first_allocnode(heap); node; node = mm_next_allocnode(node)) {
		struct alloc_node_info_s *item;
		void *data = (char *)node + SIZEOF_MM_ALLOCNODE;
		if (data == g_node_info || data == g_hash_table) { continue; }
		/* An incomplete table must never produce a no-leaks result. */
		if (count == MAX_ALLOC_COUNT || node->size < SIZEOF_MM_ALLOCNODE) {
			return ERROR;
		}
		item = &g_node_info[count];
		item->node = (uintptr_t)node;
		item->next = NULL;
		item->data = data;
		item->size = node->size - SIZEOF_MM_ALLOCNODE;
		item->caller = node->alloc_call_addr;
		item->pid = node->pid;
		item->used = false;
		add_hash(count++);
	}
	*node_count = count;
	return OK;
}

static void search_addr(void *start_addr, void *end_addr, int *leak_cnt)
{
	/* This function traverse the memory from start_addr to end_addr for comparing the address based on hash table. */
	const unsigned char *leak_chk;
	uintptr_t start = (uintptr_t)start_addr;
	uintptr_t end = (uintptr_t)end_addr;
	if (end < start || end - start < sizeof(uintptr_t)) { return; }
	for (leak_chk = start_addr; (uintptr_t)leak_chk <= end - sizeof(uintptr_t); leak_chk++) {
		uintptr_t word;
		memcpy(&word, leak_chk, sizeof(word));
		if (search_hash(word - (uintptr_t)SIZEOF_MM_ALLOCNODE)) {
			(*leak_cnt)--;
		}
	}
}

static void heap_check(struct mm_heap_s *heap, int checker_pid, int *leak_cnt)
{
	void *leak_chk;
	struct mm_allocnode_s *visit_node;
	void *exclude_top;
	void *exclude_bottom;

	struct tcb_s *ctcb = sched_gettcb(checker_pid);
	ASSERT(ctcb != NULL);
	exclude_top = ctcb->adj_stack_ptr;
	exclude_bottom = ctcb->adj_stack_ptr - ctcb->adj_stack_size;

	/* Inspect the public diagnostic allocation list while its heap is locked. */
	{
		for (visit_node = mm_first_allocnode(heap); visit_node; visit_node = mm_next_allocnode(visit_node)) {
			if ((visit_node->preceding & MM_ALLOC_BIT) != 0) {
				if ((void *)((char *)visit_node + SIZEOF_MM_ALLOCNODE) == (void *)g_node_info) {
					continue;
				}
				for (leak_chk = (char *)visit_node + SIZEOF_MM_ALLOCNODE; (char *)leak_chk + sizeof(unsigned long) <= ((char *)visit_node) + visit_node->size; leak_chk++) {
					if ((leak_chk >= exclude_bottom && leak_chk <= exclude_top)) {
						continue;
					}
					{
						uintptr_t word;
						memcpy(&word, leak_chk, sizeof(word));
						if (search_hash(word - (uintptr_t)SIZEOF_MM_ALLOCNODE)) {
							(*leak_cnt)--;
						}
					}
				}
			}
		}
	}
}

static int ram_check(struct mm_heap_s *heap, int checker_pid, char *bin_name, int *leak_cnt)
{
	
#ifdef CONFIG_APP_BINARY_SEPARATION
	bin_addr_info_t *info;
	int bin_idx;

	info = (bin_addr_info_t *)get_bin_addr_list();
	for (bin_idx = 0; bin_idx <= CONFIG_NUM_APPS; bin_idx++) {
		if (strncmp(BIN_NAME(bin_idx), bin_name, strlen(bin_name)) == 0) {
			break;
		}
	}
#endif
	/* A stale app name must not index past the binary-address table. */
#ifdef CONFIG_APP_BINARY_SEPARATION
	if (strcmp(bin_name, "kernel") != 0 && bin_idx > CONFIG_NUM_APPS) { return ERROR; }
#endif
	/* Visit all the data regions
	 */
	int mem_region_idx;
	for (mem_region_idx = 0; mem_region_idx < MEM_VAR_REGION_COUNT; mem_region_idx++) {
		search_addr(variable_region_start_addr[mem_region_idx], variable_region_end_addr[mem_region_idx], leak_cnt);
	}

#ifdef CONFIG_APP_BINARY_SEPARATION
	if (strncmp(bin_name, "kernel", strlen("kernel") + 1) == 0) {
		/* do nothing */
	} else {
#ifdef CONFIG_SUPPORT_COMMON_BINARY
		search_addr((void *)info[CMN_BIN_IDX].data_addr, (void *)(info[CMN_BIN_IDX].data_addr + info[CMN_BIN_IDX].data_size), leak_cnt);
		search_addr((void *)info[CMN_BIN_IDX].bss_addr, (void *)(info[CMN_BIN_IDX].bss_addr + info[CMN_BIN_IDX].bss_size), leak_cnt);
#endif
		/* search the bss and data region of the loadable app */
		search_addr((void *)info[bin_idx].data_addr, (void *)(info[bin_idx].data_addr + info[bin_idx].data_size), leak_cnt);
		search_addr((void *)info[bin_idx].bss_addr, (void *)(info[bin_idx].bss_addr + info[bin_idx].bss_size), leak_cnt);
	}
#endif

	/* All kernel heaps are locked and can contain references to the target.
	 * Include the app payloads separately when examining an app heap.
	 */
	{
		struct mm_heap_s *kernel = kmm_get_baseheap();
		int index;
		for (index = 0; index < CONFIG_KMM_NHEAPS; index++) {
			heap_check(&kernel[index], checker_pid, leak_cnt);
		}
		if (heap != kernel) { heap_check(heap, checker_pid, leak_cnt); }
	}
	return OK;
}

static void print_mem_hex_dump(void *addr, size_t alloc_size)
{
	unsigned char *ptr = (unsigned char *)addr;
	size_t dump_size = (alloc_size < MEM_DUMP_MAX_BYTES) ? alloc_size : MEM_DUMP_MAX_BYTES;
	size_t i;

	printf("[DATA] ");
	for (i = 0; i < dump_size; i++) {
		printf("%02x ", ptr[i]);
		if ((i + 1) % 16 == 0 && (i + 1) < dump_size) {
			printf("\n       ");
		}
	}
	printf("\n");
}

static void print_info(int node_count, int leak_count)
{
	int i;
	if (leak_count == 0) {
		printf("*** NO MEMORY LEAK in locked snapshot.\n");
		return;
	}
	printf("Type   |    Addr    | Size(byte) |    Owner   | PID \n");
	printf("---------------------------------------------------\n");
	for (i = 0; i < node_count; i++) {
		const struct alloc_node_info_s *item = &g_node_info[i];
		pid_t pid = item->pid < 0 ? -item->pid : item->pid;
		if (item->used) { continue; }
		printf("LEAK   | %10p |  %8u  | %10p | %d\n", item->data,
			(unsigned int)item->size, item->caller, pid);
		print_mem_hex_dump((void *)item->sample, item->size);
	}
	printf("*** %d LEAKS in locked snapshot.\n", leak_count);
}

int run_mem_leak_checker(int checker_pid, char *bin_name)
{
	struct mm_heap_s *kernel = kmm_get_baseheap();
	struct mm_heap_s *heap = NULL;
	struct tcb_s *checker;
	int locked = 0;
	int node_count = 0;
	int leak_count = 0;
	int cancel_state;
	int i;
	int ret = ERROR;
	bool app_locked = false;
	const char *failure = "heap busy or unavailable";

	if (task_setcancelstate(TASK_CANCEL_DISABLE, &cancel_state) != OK) { return ERROR; }
	if (!claim_checker()) {
		printf("mem_leak_checker is already running.\n");
		task_setcancelstate(cancel_state, NULL);
		return ERROR;
	}
	/* Allocation happens before any heap lock. The report and 32-byte data
	 * samples share this bounded buffer; no malloc/printf occurs in the scan.
	 */
	if (hash_init() != OK) {
		printf("hash table memory alloc is failed.\n");
		goto done;
	}

	/* This port supports UP. Nonblocking locks plus scheduler exclusion avoid
	 * holding one heap while waiting for a task holding another. IRQ frees
	 * are deferred by mm_free and cannot remove the locked metadata nodes.
	 */
	sched_lock();
	checker = sched_gettcb(checker_pid);
	if (!kernel || !checker) { goto unlock; }
	for (i = 0; i < CONFIG_KMM_NHEAPS; i++) {
		if (mm_trysemaphore(&kernel[i]) != OK) { goto unlock; }
		locked++;
	}
	if (strcmp(bin_name, "kernel") == 0) {
		heap = kernel;
	}
#ifdef CONFIG_APP_BINARY_SEPARATION
	else {
		/* Resolve the app handle only after other threads cannot unload it. */
		heap = mm_get_app_heap_with_name(bin_name);
		if (!heap || mm_trysemaphore(heap) != OK) { goto unlock; }
		app_locked = true;
	}
#endif
	if (!heap) { goto unlock; }
	failure = "heap corruption or snapshot capacity exceeded";
	if (heap == kernel) {
		for (i = 0; i < CONFIG_KMM_NHEAPS; i++) {
			if (fill_hash_table(&kernel[i], &node_count) != OK) { goto unlock; }
		}
	} else if (fill_hash_table(heap, &node_count) != OK) { goto unlock; }
	leak_count = node_count;
	if (ram_check(heap, checker_pid, bin_name, &leak_count) != OK) {
		failure = "binary address information unavailable";
		goto unlock;
	}
	for (i = 0; i < node_count; i++) {
		struct alloc_node_info_s *item = &g_node_info[i];
		size_t bytes = item->size < MEM_DUMP_MAX_BYTES ? item->size : MEM_DUMP_MAX_BYTES;
		if (!item->used) { memcpy(item->sample, item->data, bytes); }
	}
	ret = OK;

unlock:
	if (app_locked) { mm_givesemaphore(heap); }
	while (locked > 0) { mm_givesemaphore(&kernel[--locked]); }
	sched_unlock();
	/* Only copied metadata and bytes are touched after releasing the locks. */
	if (ret == OK) { print_info(node_count, leak_count); }
	else { printf("mem_leak_checker failed: %s; no complete result.\n", failure); }
	hash_deinit();
done:
	release_checker();
	task_setcancelstate(cancel_state, NULL);
	return ret;
}

int run_all_mem_leak_checker(int checker_pid)
{
	int ret;
	printf("\nKernel :\n");
	ret = run_mem_leak_checker(checker_pid, "kernel");

	if (ret != OK) {
		return ERROR;
	}

#ifdef CONFIG_APP_BINARY_SEPARATION
	printf("\nBelow are text addresses of loadable apps (and common binary if enabled) :\n");
	printf("The pc value of the allocation can be obtained by subtracting the text start address of the appropriate binary\n\n");
	bin_addr_info_t *bin_addr_info = (bin_addr_info_t *)get_bin_addr_list();
	int bin_idx;
	for (bin_idx = 0; bin_idx <= CONFIG_NUM_APPS; bin_idx++) {
		if (bin_addr_info[bin_idx].text_addr != 0) {
			printf("[%s] Text Addr : %p, Text Size : %u\n", BIN_NAME(bin_idx), bin_addr_info[bin_idx].text_addr, bin_addr_info[bin_idx].text_size);
		}
	}
	printf("\n");
	/* bin_idx value zero is always reserved for common binary, so
	 * skip checking common binary and start checking from index one
	 */
	for (bin_idx = 1; bin_idx <= CONFIG_NUM_APPS; bin_idx++) {
		if (bin_addr_info[bin_idx].text_addr != 0) {
			printf("%s :\n", BIN_NAME(bin_idx));
			ret = run_mem_leak_checker(checker_pid, BIN_NAME(bin_idx));
			if (ret != OK) {
				return ERROR;
			}
		}
	}
#endif
	return OK;
}
