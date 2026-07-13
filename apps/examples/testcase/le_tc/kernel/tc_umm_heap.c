/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
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

/// @file tc_umm_heap.c

/// @brief Test Case Example for Umm Heap API

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <tinyara/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/wait.h>
#include <tinyara/mm/mm.h>
#include <tinyara/sched.h>
#include "tc_internal.h"

/****************************************************************************
 * Definitions
 ****************************************************************************/

#define ALLOC_SIZE_VAL 10
#define ALLOC_FREE_TIMES 5
#define TEST_TIMES 100
#define ALL_FREE 0
#define MEM_REQ_SIZE(unit, iter) (MM_ALIGN_UP((unit) + SIZEOF_MM_ALLOCNODE) * (iter))

#ifdef CONFIG_MM_SMALL_ALLOC_POOL
#if defined(CONFIG_MM_SMALL_ALLOC_POOL_64)
#define TC_UMM_SMALL_POOL_MAX_SIZE 64
#elif defined(CONFIG_MM_SMALL_ALLOC_POOL_32)
#define TC_UMM_SMALL_POOL_MAX_SIZE 32
#else
#define TC_UMM_SMALL_POOL_MAX_SIZE 16
#endif
#define TC_UMM_SMALL_POOL_FALLBACK_SIZE (TC_UMM_SMALL_POOL_MAX_SIZE + 1)

#define TC_UMM_ASSERT_POOL_MALLINFO_ALLOC(api_name, cur, prev, freeResource) \
	do { \
		TC_ASSERT_GT_CLEANUP(api_name, (cur).uordblks, (prev).uordblks, freeResource); \
		TC_ASSERT_GT_CLEANUP(api_name, (prev).fordblks, (cur).fordblks, freeResource); \
		TC_ASSERT_EQ_CLEANUP(api_name, (cur).uordblks - (prev).uordblks, \
		                     (prev).fordblks - (cur).fordblks, freeResource); \
	} while (0)

#define TC_UMM_ASSERT_POOL_HEAPINFO_ALLOC(api_name, curr_alloc_size, freeResource) \
	TC_ASSERT_GEQ_CLEANUP(api_name, curr_alloc_size, CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE, freeResource)

#ifdef CONFIG_DEBUG_MM_HEAPINFO
#define TC_UMM_SMALL_POOL_HEAPINFO_DECL \
	struct mm_heap_s *heap; \
	pid_t hash_pid = PIDHASH(getpid())

#define TC_UMM_ASSERT_SMALL_POOL_HEAPINFO(mem, freeResource) \
	do { \
		heap = umm_get_heap(mem); \
		TC_ASSERT_NEQ_CLEANUP("umm_get_heap", heap, NULL, freeResource); \
		TC_UMM_ASSERT_POOL_HEAPINFO_ALLOC("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size, \
		                                  freeResource); \
	} while (0)

#define TC_UMM_ASSERT_SMALL_POOL_HEAPINFO_FREE() \
	TC_ASSERT_EQ("small_pool", heap->alloc_list[hash_pid].curr_alloc_size, ALL_FREE)
#else
#define TC_UMM_SMALL_POOL_HEAPINFO_DECL
#define TC_UMM_ASSERT_SMALL_POOL_HEAPINFO(mem, freeResource)
#define TC_UMM_ASSERT_SMALL_POOL_HEAPINFO_FREE()
#endif

#define TC_UMM_ASSERT_SMALL_POOL_CLASS(first_size, second_size, mem_ptr) \
	do { \
		struct mallinfo class_prev; \
		struct mallinfo class_cur; \
		struct mallinfo class_partial; \
		struct mallinfo class_reuse; \
		struct mallinfo class_released; \
		TC_UMM_SMALL_POOL_HEAPINFO_DECL; \
		tc_umm_heap_get_mallinfo(&class_prev); \
		(mem_ptr)[0] = (int *)malloc(first_size); \
		TC_ASSERT_NEQ("malloc", (mem_ptr)[0], NULL); \
		tc_umm_heap_get_mallinfo(&class_cur); \
		TC_ASSERT_GT_CLEANUP("small_pool", class_prev.fordblks, class_cur.fordblks, TC_FREE_MEMORY((mem_ptr)[0])); \
		TC_ASSERT_GEQ_CLEANUP("small_pool", class_prev.fordblks - class_cur.fordblks, \
		                      CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE, TC_FREE_MEMORY((mem_ptr)[0])); \
		TC_UMM_ASSERT_SMALL_POOL_HEAPINFO((mem_ptr)[0], TC_FREE_MEMORY((mem_ptr)[0])); \
		(mem_ptr)[1] = (int *)malloc(second_size); \
		TC_ASSERT_NEQ_CLEANUP("malloc", (mem_ptr)[1], NULL, TC_FREE_MEMORY((mem_ptr)[0])); \
		tc_umm_heap_get_mallinfo(&class_reuse); \
		TC_ASSERT_EQ_CLEANUP("small_pool", class_reuse.fordblks, class_cur.fordblks, \
		                     mem_deallocate_func(mem_ptr, 2)); \
		free((mem_ptr)[0]); \
		(mem_ptr)[0] = NULL; \
		tc_umm_heap_get_mallinfo(&class_partial); \
		TC_ASSERT_EQ_CLEANUP("small_pool", class_partial.fordblks, class_reuse.fordblks, \
		                     mem_deallocate_func(mem_ptr, 2)); \
		free((mem_ptr)[1]); \
		(mem_ptr)[1] = NULL; \
		tc_umm_heap_get_mallinfo(&class_released); \
		TC_ASSERT_EQ("small_pool", class_released.uordblks, class_prev.uordblks); \
		TC_ASSERT_EQ("small_pool", class_released.fordblks, class_prev.fordblks); \
		TC_UMM_ASSERT_SMALL_POOL_HEAPINFO_FREE(); \
	} while (0)
#endif

/****************************************************************************
 * Private functions
 ****************************************************************************/

#ifdef CONFIG_MM_SMALL_ALLOC_POOL
static void tc_umm_heap_get_mallinfo(struct mallinfo *info)
{
#ifdef CONFIG_CAN_PASS_STRUCTS
	*info = mallinfo();
#else
	(void)mallinfo(info);
#endif
}
#endif

static void mem_deallocate_func(int *mem_arr[], int dealloc_size)
{
	int dealloc_cnt;
	for (dealloc_cnt = 0; dealloc_cnt < dealloc_size; dealloc_cnt++) {
		free(mem_arr[dealloc_cnt]);
		mem_arr[dealloc_cnt] = NULL;
	}
}

/**
* @fn                   :tc_umm_heap_malloc_free
* @brief                :Allocate memory through malloc and free it.
* @scenario             :Allocate memory through malloc\n
*                        free allocated memory
* @API's covered        :malloc, free
* @passcase             :When malloc function returns non null memory and memory is null after free.
* @failcase             :When malloc function returns null memory or memory is not null after free.
* @Preconditions        :NA
*/

static void tc_umm_heap_malloc_free(void)
{
	int *mem_ptr[ALLOC_FREE_TIMES] = { NULL };
	int n_alloc;
	int n_test_iter;
	size_t alloc_size = ALLOC_SIZE_VAL * sizeof(int);
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	pid_t hash_pid = PIDHASH(getpid());;
	struct mm_heap_s *heap;
#endif

	/* Iteration test */

	for (n_test_iter = 0; n_test_iter < TEST_TIMES; n_test_iter++) {
		/* Allocate memory */

		for (n_alloc = 0; n_alloc < ALLOC_FREE_TIMES; n_alloc++) {
			mem_ptr[n_alloc] = (int *)malloc(alloc_size);
			TC_ASSERT_NEQ("malloc", mem_ptr[n_alloc], NULL);
		}

#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Verify Allocation */

		heap = umm_get_heap(mem_ptr[n_alloc - 1]);
		TC_ASSERT_NEQ_CLEANUP("umm_get_heap", heap, NULL, mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#ifdef CONFIG_MM_SMALL_ALLOC_POOL
		TC_UMM_ASSERT_POOL_HEAPINFO_ALLOC("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size,
		                                  mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#else
		TC_ASSERT_EQ_ERROR_CLEANUP("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size,
		          MEM_REQ_SIZE(alloc_size, ALLOC_FREE_TIMES), get_errno(), mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#endif
#endif

		/* Free allocated memory */

		mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES);

#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Verify freeing */

		TC_ASSERT_EQ("mem_deallocate_func", heap->alloc_list[hash_pid].curr_alloc_size, ALL_FREE);
#endif
	}

#if !defined(CONFIG_MM_ASSERT_ON_FAIL)
	/* Test unavailable allocation due to big size */
	struct mallinfo minfo;
#ifdef CONFIG_CAN_PASS_STRUCTS
	minfo = mallinfo();
#else
	(void)mallinfo(&minfo);
#endif
	mem_ptr[0] = (int *)malloc(minfo.mxordblk + 1);
	TC_ASSERT_EQ_CLEANUP("malloc", mem_ptr[0], NULL, free(mem_ptr[0]));
#endif

	TC_SUCCESS_RESULT();
}

/**
* @fn                   :tc_umm_heap_calloc
* @brief                :Allocate memory through calloc and free it.
* @scenario             :Allocate memory through calloc\n
*                        free allocated memory
* @API's covered        :calloc, free
* @passcase             :When calloc function returns non null memory.
* @failcase             :When calloc function returns null memory.
* @Preconditions        :NA
*/
static void tc_umm_heap_calloc(void)
{
	int *mem_ptr[ALLOC_FREE_TIMES] = { NULL };
	int n_alloc;
	int n_test_iter;
#ifdef CONFIG_DEBUG_MM_HEAPINFO
#ifndef CONFIG_MM_SMALL_ALLOC_POOL
	size_t alloc_size = ALLOC_SIZE_VAL * sizeof(int);
#endif
	pid_t hash_pid = PIDHASH(getpid());;
	struct mm_heap_s *heap;
#endif

	/* Iteration test */

	for (n_test_iter = 0; n_test_iter < TEST_TIMES; n_test_iter++) {
		/* Allocate memory */

		for (n_alloc = 0; n_alloc < ALLOC_FREE_TIMES; n_alloc++) {
			mem_ptr[n_alloc] = (int *)calloc(ALLOC_SIZE_VAL, sizeof(int));
			TC_ASSERT_NEQ("calloc", mem_ptr[n_alloc], NULL);
		}

#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Verify Allocation */

		heap = umm_get_heap(mem_ptr[n_alloc - 1]);
		TC_ASSERT_NEQ_CLEANUP("umm_get_heap", heap, NULL, mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#ifdef CONFIG_MM_SMALL_ALLOC_POOL
		TC_UMM_ASSERT_POOL_HEAPINFO_ALLOC("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size,
		                                  mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#else
		TC_ASSERT_EQ_ERROR_CLEANUP("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size,
		          MEM_REQ_SIZE(alloc_size, ALLOC_FREE_TIMES), get_errno(), mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#endif
#endif

		/* Free allocated memory */

		mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES);

#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Verify freeing */

		TC_ASSERT_EQ("mem_deallocate_func", heap->alloc_list[hash_pid].curr_alloc_size, ALL_FREE);
#endif
	}
	TC_SUCCESS_RESULT();
}

/**
* @fn                   :tc_umm_heap_realloc
* @brief                :Allocate memory through malloc and free it.
* @scenario             :Allocate memory through malloc\n
*                        Reallocate memory through malloc\n
*                        free allocated memory
* @API's covered        :malloc, realloc, free
* @passcase             :When realloc function returns non null memory.
* @failcase             :When realloc function returns null memory.
* @Preconditions        :malloc
*/
static void tc_umm_heap_realloc(void)
{
	int *mem_ptr[ALLOC_FREE_TIMES] = { NULL };
	int *prev_mem = NULL;
	int n_alloc;
	int n_test_iter;
	size_t alloc_size = ALLOC_SIZE_VAL * sizeof(int);
	struct mallinfo prev;
	struct mallinfo cur;
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	pid_t hash_pid = PIDHASH(getpid());;
	struct mm_heap_s *heap;
#endif

	/* Iteration test */

	for (n_test_iter = 0; n_test_iter < TEST_TIMES; n_test_iter++) {
		/* Save mallinfo before test start */

#ifdef CONFIG_CAN_PASS_STRUCTS
		prev = mallinfo();
#else
		(void)mallinfo(&prev);
#endif

		/* Allocate memory */

		for (n_alloc = 0; n_alloc < ALLOC_FREE_TIMES; n_alloc++) {
			mem_ptr[n_alloc] = (int *)realloc(prev_mem, alloc_size);
			TC_ASSERT_NEQ("realloc", mem_ptr[n_alloc], NULL);
		}

		/* Verify allocation */

#ifdef CONFIG_CAN_PASS_STRUCTS
		cur = mallinfo();
#else
		(void)mallinfo(&cur);
#endif
		// Due to remain size, it could be greater than sizeof(int) + SIZEOF_MM_ALLOCNODE.
#ifdef CONFIG_MM_SMALL_ALLOC_POOL
		TC_UMM_ASSERT_POOL_MALLINFO_ALLOC("mallinfo", cur, prev, mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#else
		TC_ASSERT_EQ_CLEANUP("mallinfo", cur.uordblks - prev.uordblks, MEM_REQ_SIZE(alloc_size, ALLOC_FREE_TIMES),
		                     mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
		TC_ASSERT_EQ_CLEANUP("mallinfo", prev.fordblks - cur.fordblks, MEM_REQ_SIZE(alloc_size, ALLOC_FREE_TIMES),
                             mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#endif

#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Verify allocation by heapinfo */

		heap = umm_get_heap(mem_ptr[n_alloc - 1]);
		TC_ASSERT_NEQ_CLEANUP("umm_get_heap", heap, NULL, mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#ifdef CONFIG_MM_SMALL_ALLOC_POOL
		TC_UMM_ASSERT_POOL_HEAPINFO_ALLOC("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size,
		                                  mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#else
		TC_ASSERT_EQ_ERROR_CLEANUP("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size,
		          MEM_REQ_SIZE(alloc_size, ALLOC_FREE_TIMES), get_errno(), mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#endif
#endif

		/* Free allocated memory */

		mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES);

		/* Verify freeing */

#ifdef CONFIG_CAN_PASS_STRUCTS
		cur = mallinfo();
#else
		(void)mallinfo(&cur);
#endif
		TC_ASSERT_EQ("mallinfo", cur.uordblks - prev.uordblks, 0);
		TC_ASSERT_EQ("mallinfo", prev.fordblks - cur.fordblks, 0);

#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Verify freeing by heapinfo */

		TC_ASSERT_EQ("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size, ALL_FREE);
#endif
	}

	/* Relloc Free by size 0 */

	mem_ptr[0] = (int *)malloc(10);
	mem_ptr[1] = (int *)realloc(mem_ptr[0], 0);
	TC_ASSERT_EQ_CLEANUP("realloc", mem_ptr[1], NULL, free(mem_ptr[0]));

	/* Verify freeing */

#ifdef CONFIG_CAN_PASS_STRUCTS
	cur = mallinfo();
#else
	(void)mallinfo(&cur);
#endif
	TC_ASSERT_EQ("mallinfo", cur.uordblks - prev.uordblks, 0);
	TC_ASSERT_EQ("mallinfo", prev.fordblks - cur.fordblks, 0);

	/* Relloc Free by size 0 */
	mem_ptr[0] = (int *)malloc(alloc_size);
	TC_ASSERT_NEQ("malloc", mem_ptr[0], NULL);
	alloc_size /= 2;
	mem_ptr[1] = (int *)realloc(mem_ptr[0], alloc_size);
	TC_ASSERT_NEQ_CLEANUP("realloc", mem_ptr[1], NULL, free(mem_ptr[0]));

	/* Verify freeing */

#ifdef CONFIG_CAN_PASS_STRUCTS
	cur = mallinfo();
#else
	(void)mallinfo(&cur);
#endif
#ifdef CONFIG_MM_SMALL_ALLOC_POOL
	TC_UMM_ASSERT_POOL_MALLINFO_ALLOC("mallinfo", cur, prev, TC_FREE_MEMORY(mem_ptr[1]));
#else
	TC_ASSERT_EQ("mallinfo", cur.uordblks - prev.uordblks, MEM_REQ_SIZE(alloc_size, 1));
	TC_ASSERT_EQ("mallinfo", prev.fordblks - cur.fordblks, MEM_REQ_SIZE(alloc_size, 1));
#endif

	free(mem_ptr[1]);

#ifdef CONFIG_CAN_PASS_STRUCTS
	cur = mallinfo();
#else
	(void)mallinfo(&cur);
#endif
	TC_ASSERT_EQ("mallinfo", cur.uordblks - prev.uordblks, 0);
	TC_ASSERT_EQ("mallinfo", prev.fordblks - cur.fordblks, 0);

	TC_SUCCESS_RESULT();
}

/**
* @fn                   :tc_umm_heap_memalign
* @brief                :Allocate memory through memalign and free it.
* @scenario             :Allocate memory through memalign\n
*                        free allocated memory
* @API's covered        :memalign, free
* @passcase             :When memalign function returns non null memory.
* @failcase             :When memalign function returns null memory.
* @Preconditions        :NA
*/
static void tc_umm_heap_memalign(void)
{
	int *mem_ptr[ALLOC_FREE_TIMES] = { NULL };
	int n_alloc;
	int n_test_iter;
	size_t alloc_size = ALLOC_SIZE_VAL * sizeof(int);
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	pid_t hash_pid = PIDHASH(getpid());;
	struct mm_heap_s *heap;
#endif

	/* Iteration test */

	for (n_test_iter = 0; n_test_iter < TEST_TIMES; n_test_iter++) {
		/* Allocate memory */

		for (n_alloc = 0; n_alloc < ALLOC_FREE_TIMES; n_alloc++) {
			mem_ptr[n_alloc] = (int *)memalign(sizeof(int), alloc_size);
			TC_ASSERT_NEQ("memalign", mem_ptr[n_alloc], NULL);
		}

#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Verify Allocation */

		heap = umm_get_heap(mem_ptr[n_alloc - 1]);
		TC_ASSERT_NEQ_CLEANUP("umm_get_heap", heap, NULL, mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
		TC_ASSERT_EQ_ERROR_CLEANUP("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size,
		          MEM_REQ_SIZE(alloc_size, ALLOC_FREE_TIMES), get_errno(), mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#endif

		/* Free allocated memory */

		mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES);

#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Verify freeing */

		TC_ASSERT_EQ("mem_deallocate_func", heap->alloc_list[hash_pid].curr_alloc_size, ALL_FREE);
#endif
	}
	TC_SUCCESS_RESULT();
}

/**
* @fn                   :tc_umm_heap_mallinfo
* @brief                :Gives current heap information for the user heap.
* @scenario             :Allocate memory through malloc\n
*                        Gives current heap information for the user heap
*                        Free allocated memory
* @API's covered        :malloc, mallinfo, free
* @passcase             :When malloc function returns non null memory and mallinfo gives non null information.
* @failcase             :When malloc function returns null memory or mallinfo gives null information.
* @Preconditions        :malloc
*/
static void tc_umm_heap_mallinfo(void)
{
	int *mem_ptr = NULL;
	int n_test_iter;
	size_t alloc_size = sizeof(int);
	struct mallinfo prev;
	struct mallinfo cur;

	for (n_test_iter = 0; n_test_iter < TEST_TIMES; n_test_iter++) {
		/* Save mallinfo before test start */

#ifdef CONFIG_CAN_PASS_STRUCTS
		prev = mallinfo();
#else
		(void)mallinfo(&prev);
#endif

		/* Allocate memory */

		mem_ptr = (int *)malloc(alloc_size);
		TC_ASSERT_NEQ("malloc", mem_ptr, NULL);

		/* Verify allocation */

#ifdef CONFIG_CAN_PASS_STRUCTS
		cur = mallinfo();
#else
		(void)mallinfo(&cur);
#endif
		// Due to remain size, it could be greater than sizeof(int) + SIZEOF_MM_ALLOCNODE.
#ifdef CONFIG_MM_SMALL_ALLOC_POOL
		TC_UMM_ASSERT_POOL_MALLINFO_ALLOC("mallinfo", cur, prev, TC_FREE_MEMORY(mem_ptr));
#else
		TC_ASSERT_EQ_CLEANUP("mallinfo", cur.uordblks - prev.uordblks, MEM_REQ_SIZE(alloc_size, 1), TC_FREE_MEMORY(mem_ptr));
		TC_ASSERT_EQ_CLEANUP("mallinfo", prev.fordblks - cur.fordblks, MEM_REQ_SIZE(alloc_size, 1), TC_FREE_MEMORY(mem_ptr));
#endif

		TC_FREE_MEMORY(mem_ptr);

		/* Verify freeing */

#ifdef CONFIG_CAN_PASS_STRUCTS
		cur = mallinfo();
#else
		(void)mallinfo(&cur);
#endif
		TC_ASSERT_EQ("mallinfo", cur.uordblks - prev.uordblks, 0);
		TC_ASSERT_EQ("mallinfo", prev.fordblks - cur.fordblks, 0);
	}
	TC_SUCCESS_RESULT();
}

static void tc_umm_heap_zalloc(void)
{
	int *mem_ptr[ALLOC_FREE_TIMES] = { NULL };
	int n_alloc;
	int n_test_iter;
	int n_mem_ptr_idx;
	size_t alloc_size = ALLOC_SIZE_VAL * sizeof(int);
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	pid_t hash_pid = PIDHASH(getpid());;
	struct mm_heap_s *heap;
#endif

	/* Iteration test */

	for (n_test_iter = 0; n_test_iter < TEST_TIMES; n_test_iter++) {
		/* Allocate memory */

		for (n_alloc = 0; n_alloc < ALLOC_FREE_TIMES; n_alloc++) {
			mem_ptr[n_alloc] = (int *)zalloc(alloc_size);
			TC_ASSERT_NEQ("zalloc", mem_ptr[n_alloc], NULL);
			/* Verify zero allocation */

			for (n_mem_ptr_idx = 0; n_mem_ptr_idx < ALLOC_SIZE_VAL; n_mem_ptr_idx++) {
				TC_ASSERT_EQ_ERROR_CLEANUP("zalloc", mem_ptr[n_alloc][n_mem_ptr_idx], 0, get_errno(),
				                           mem_deallocate_func(mem_ptr, n_alloc + 1));
			}
		}

#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Verify Allocation */

		heap = umm_get_heap(mem_ptr[n_alloc - 1]);
		TC_ASSERT_NEQ_CLEANUP("umm_get_heap", heap, NULL, mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#ifdef CONFIG_MM_SMALL_ALLOC_POOL
		TC_UMM_ASSERT_POOL_HEAPINFO_ALLOC("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size,
		                                  mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#else
		TC_ASSERT_EQ_ERROR_CLEANUP("umm_get_heap", heap->alloc_list[hash_pid].curr_alloc_size,
		          MEM_REQ_SIZE(alloc_size, ALLOC_FREE_TIMES), get_errno(), mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES));
#endif
#endif

		/* Free allocated memory */

		mem_deallocate_func(mem_ptr, ALLOC_FREE_TIMES);

#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Verify freeing */

		TC_ASSERT_EQ("mem_deallocate_func", heap->alloc_list[hash_pid].curr_alloc_size, ALL_FREE);
#endif
	}
	TC_SUCCESS_RESULT();
}

#ifdef CONFIG_MM_SMALL_ALLOC_POOL
static void tc_umm_heap_small_alloc_pool(void)
{
	int *mem_ptr[2] = { NULL };
	char *zero_ptr = NULL;
	char *realloc_ptr = NULL;
	char *new_ptr = NULL;
	void *aligned_ptr = NULL;
	struct mallinfo prev;
	struct mallinfo cur;
	struct mallinfo released;
	size_t delta;
	int n_idx;

#ifdef CONFIG_MM_SMALL_ALLOC_POOL_16
	TC_UMM_ASSERT_SMALL_POOL_CLASS(1, 16, mem_ptr);
#endif
#ifdef CONFIG_MM_SMALL_ALLOC_POOL_32
	TC_UMM_ASSERT_SMALL_POOL_CLASS(17, 32, mem_ptr);
#endif
#ifdef CONFIG_MM_SMALL_ALLOC_POOL_64
	TC_UMM_ASSERT_SMALL_POOL_CLASS(33, 64, mem_ptr);
#endif

	tc_umm_heap_get_mallinfo(&prev);
	mem_ptr[0] = (int *)malloc(TC_UMM_SMALL_POOL_FALLBACK_SIZE);
	TC_ASSERT_NEQ("malloc", mem_ptr[0], NULL);
	tc_umm_heap_get_mallinfo(&cur);
	delta = prev.fordblks - cur.fordblks;
	TC_ASSERT_EQ_CLEANUP("malloc", delta, MEM_REQ_SIZE(TC_UMM_SMALL_POOL_FALLBACK_SIZE, 1),
	                     TC_FREE_MEMORY(mem_ptr[0]));
	TC_ASSERT_LT_CLEANUP("malloc", delta, CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE, TC_FREE_MEMORY(mem_ptr[0]));
	TC_FREE_MEMORY(mem_ptr[0]);
	tc_umm_heap_get_mallinfo(&released);
	TC_ASSERT_EQ("malloc", released.fordblks, prev.fordblks);

#ifdef CONFIG_MM_SMALL_ALLOC_POOL_32
	zero_ptr = (char *)zalloc(30);
	TC_ASSERT_NEQ("zalloc", zero_ptr, NULL);
	for (n_idx = 0; n_idx < 30; n_idx++) {
		TC_ASSERT_EQ_CLEANUP("zalloc", zero_ptr[n_idx], 0, TC_FREE_MEMORY(zero_ptr));
	}
	TC_FREE_MEMORY(zero_ptr);
#endif

#if defined(CONFIG_MM_SMALL_ALLOC_POOL_16) && defined(CONFIG_MM_SMALL_ALLOC_POOL_32)
	realloc_ptr = (char *)malloc(16);
	TC_ASSERT_NEQ("malloc", realloc_ptr, NULL);
	for (n_idx = 0; n_idx < 16; n_idx++) {
		realloc_ptr[n_idx] = (char)(n_idx + 1);
	}
	new_ptr = (char *)realloc(realloc_ptr, 30);
	TC_ASSERT_NEQ_CLEANUP("realloc", new_ptr, NULL, TC_FREE_MEMORY(realloc_ptr));
	TC_ASSERT_NEQ_CLEANUP("realloc", new_ptr, realloc_ptr, TC_FREE_MEMORY(new_ptr));
	realloc_ptr = new_ptr;
	for (n_idx = 0; n_idx < 16; n_idx++) {
		TC_ASSERT_EQ_CLEANUP("realloc", realloc_ptr[n_idx], (char)(n_idx + 1), TC_FREE_MEMORY(realloc_ptr));
	}
	TC_FREE_MEMORY(realloc_ptr);
#endif

	tc_umm_heap_get_mallinfo(&prev);
	aligned_ptr = memalign(64, 30);
	TC_ASSERT_NEQ("memalign", aligned_ptr, NULL);
	tc_umm_heap_get_mallinfo(&cur);
	delta = prev.fordblks - cur.fordblks;
	TC_ASSERT_EQ_CLEANUP("memalign", (uintptr_t)aligned_ptr & 63, 0, TC_FREE_MEMORY(aligned_ptr));
	TC_ASSERT_GT_CLEANUP("memalign", delta, 0, TC_FREE_MEMORY(aligned_ptr));
	TC_ASSERT_LT_CLEANUP("memalign", delta, CONFIG_MM_SMALL_ALLOC_POOL_CHUNK_SIZE, TC_FREE_MEMORY(aligned_ptr));
	TC_FREE_MEMORY(aligned_ptr);
	tc_umm_heap_get_mallinfo(&released);
	TC_ASSERT_EQ("memalign", released.fordblks, prev.fordblks);

	TC_SUCCESS_RESULT();
}
#endif
#ifdef CONFIG_DEBUG_MM_HEAPINFO
static void tc_umm_heap_get_heap_free_size(void)
{
	size_t free_size = umm_get_heap_free_size();
	struct mallinfo info;

#ifdef CONFIG_CAN_PASS_STRUCTS
	info = mallinfo();
#else
	(void)mallinfo(&info);
#endif
	TC_ASSERT_EQ("umm_get_heap_free_size", free_size, info.fordblks);

	TC_SUCCESS_RESULT();
}

static void tc_umm_heap_get_largest_freenode_size(void)
{
	size_t largest_size = umm_get_largest_freenode_size();
	struct mallinfo info;

#ifdef CONFIG_CAN_PASS_STRUCTS
	info = mallinfo();
#else
	(void)mallinfo(&info);
#endif
	TC_ASSERT_EQ("umm_get_largest_freenode_size", largest_size, info.mxordblk);
	TC_SUCCESS_RESULT();
}
#endif

static int umm_test(int argc, char *argv[])
{
	sched_lock();  // To prevent other thread allocation mixing in mallinfo

	tc_umm_heap_malloc_free();
	tc_umm_heap_calloc();
	tc_umm_heap_realloc();
	tc_umm_heap_memalign();
	tc_umm_heap_mallinfo();
	tc_umm_heap_zalloc();
#ifdef CONFIG_MM_SMALL_ALLOC_POOL
	tc_umm_heap_small_alloc_pool();
#endif
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	tc_umm_heap_get_heap_free_size();
	tc_umm_heap_get_largest_freenode_size();
#endif

	sched_unlock();

	return 0;
}

/****************************************************************************
 * Public functions
 ****************************************************************************/

int umm_heap_main(void)
{
	int pid;
	int stat_loc;
	pid = task_create("umm_test", 150, 2048, umm_test, (char * const *)NULL);
	pid = waitpid(pid, &stat_loc, 0);	// wait umm_test task termination for atomic test
	if (pid < 0) {
		sleep(5);
	}

	return 0;
}
