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

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <tinyara/mm/mm.h>
#include <tinyara/os_api_test_drv.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TEST_KMM_ALLOC_SIZE	64
#define TEST_KMM_REALLOC_SIZE	128
#define TEST_KMM_CALLOC_COUNT	4
#define TEST_KMM_ALIGN		32
#define TEST_KMM_PATTERN	0xa5

enum test_kmm_heap_case_e {
	TEST_KMM_HEAP_ALL = 0,
	TEST_KMM_HEAP_NEGATIVE_INDEX,
	TEST_KMM_HEAP_ZERO_INDEX,
	TEST_KMM_HEAP_NONZERO_INDEX,
	TEST_KMM_HEAP_UPPER_BOUND_INDEX,
	TEST_KMM_HEAP_USER_ADDRESS,
	TEST_KMM_HEAP_ALLOC_FAIL_TRAVERSAL,
	TEST_KMM_HEAP_ALLOCATIONS,
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_MM_KERNEL_HEAP
static int g_test_kmm_nonheap_marker;

#if defined(CONFIG_APP_BINARY_SEPARATION) && defined(CONFIG_DEBUG_MM_HEAPINFO)
extern void mm_manage_alloc_fail_dump(struct mm_heap_s *heap, int startidx,
	int endidx, size_t size, size_t align, int heap_type,
	mmaddress_t caller);
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_kmm_get_mallinfo(FAR struct mallinfo *info)
{
#ifdef CONFIG_CAN_PASS_STRUCTS
	*info = kmm_mallinfo();
	return OK;
#else
	return kmm_mallinfo(info);
#endif
}

static int test_kmm_is_zero(FAR const unsigned char *mem, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (mem[i] != 0) {
			return ERROR;
		}
	}

	return OK;
}

static int test_kmm_pattern_kept(FAR const unsigned char *mem, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (mem[i] != TEST_KMM_PATTERN) {
			return ERROR;
		}
	}

	return OK;
}

static int test_kmm_lock_heaps(FAR struct mm_heap_s *baseheap)
{
	int heap_index;

	for (heap_index = 0; heap_index < CONFIG_KMM_NHEAPS; heap_index++) {
		if (!mm_takesemaphore(&baseheap[heap_index])) {
			while (heap_index > 0) {
				heap_index--;
				mm_givesemaphore(&baseheap[heap_index]);
			}

			return ERROR;
		}
	}

	return OK;
}

static void test_kmm_unlock_heaps(FAR struct mm_heap_s *baseheap)
{
	int heap_index;

	for (heap_index = CONFIG_KMM_NHEAPS; heap_index > 0; heap_index--) {
		mm_givesemaphore(&baseheap[heap_index - 1]);
	}
}

static int test_kmm_heap_negative_index(void)
{
	if (kmm_get_heap_with_index(-1) != NULL ||
		kmm_get_heap_with_index(INT_MIN) != NULL) {
		dbg("kmm_get_heap_with_index accepted a negative index.\n");
		return ERROR;
	}

	return OK;
}

static int test_kmm_heap_zero_index(void)
{
	FAR struct mm_heap_s *baseheap = kmm_get_baseheap();

	if (baseheap == NULL || kmm_get_heap_with_index(0) != baseheap) {
		dbg("kmm_get_heap_with_index did not return heap zero.\n");
		return ERROR;
	}

	return OK;
}

static int test_kmm_heap_nonzero_index(void)
{
#if CONFIG_KMM_NHEAPS > 1
	FAR struct mm_heap_s *baseheap = kmm_get_baseheap();

	if (baseheap == NULL || kmm_get_heap_with_index(1) != &baseheap[1]) {
		dbg("kmm_get_heap_with_index did not return heap one.\n");
		return ERROR;
	}

	return OK;
#else
	return -ENOSYS;
#endif
}

static int test_kmm_heap_upper_bound_index(void)
{
	if (kmm_get_heap_with_index(CONFIG_KMM_NHEAPS) != NULL ||
		kmm_get_heap_with_index(INT_MAX) != NULL) {
		dbg("kmm_get_heap_with_index accepted an upper-bound index.\n");
		return ERROR;
	}

	return OK;
}

static int test_kmm_heap_user_address(void)
{
#ifdef CONFIG_BUILD_PROTECTED
	FAR void *mem = malloc(TEST_KMM_ALLOC_SIZE);

	if (mem == NULL) {
		return ERROR;
	}

	if (kmm_get_heap(mem) != NULL ||
		kmm_get_index_of_heap(mem) != INVALID_HEAP_IDX) {
		free(mem);
		dbg("kernel heap lookup accepted a user-heap address.\n");
		return ERROR;
	}

	free(mem);
	return OK;
#else
	return -ENOSYS;
#endif
}

static int test_kmm_alloc_fail_traversal(void)
{
#if defined(CONFIG_APP_BINARY_SEPARATION) && defined(CONFIG_DEBUG_MM_HEAPINFO)
	FAR struct mm_heap_s *baseheap = kmm_get_baseheap();

	if (baseheap == NULL) {
		return ERROR;
	}

	mm_manage_alloc_fail_dump(baseheap, HEAP_START_IDX, HEAP_END_IDX,
		TEST_KMM_ALLOC_SIZE, 0, KERNEL_HEAP, NULL);
	return OK;
#else
	return -ENOSYS;
#endif
}

static int test_kmm_heap(unsigned long arg)
{
	FAR struct mm_heap_s *baseheap;
	FAR struct mm_heap_s *heap;
	struct mallinfo info;
	FAR unsigned char *mem = NULL;
	FAR unsigned char *newmem;
	FAR unsigned char *zmem = NULL;
	FAR unsigned char *cmem = NULL;
	FAR unsigned char *aligned = NULL;
	size_t largest;
	int heap_index;
	int ret = ERROR;

	(void)arg;

	baseheap = kmm_get_baseheap();
	if (baseheap == NULL) {
		return ERROR;
	}

	if (test_kmm_lock_heaps(baseheap) != OK) {
		return ERROR;
	}

	if (test_kmm_get_mallinfo(&info) != OK) {
		goto errout_with_heap_lock;
	}

	largest = kmm_get_largest_freenode_size();
	if (largest != info.mxordblk) {
		goto errout_with_heap_lock;
	}

#ifdef CONFIG_DEBUG_MM_HEAPINFO
	if (kmm_get_heap_free_size() != info.fordblks) {
		goto errout_with_heap_lock;
	}
#endif

	test_kmm_unlock_heaps(baseheap);

	if (kmm_get_heap(&g_test_kmm_nonheap_marker) != NULL ||
		kmm_get_index_of_heap(&g_test_kmm_nonheap_marker) != INVALID_HEAP_IDX ||
		kmm_heapmember(&g_test_kmm_nonheap_marker)) {
		goto errout;
	}

	if (mm_check_heap_corruption(baseheap) != OK) {
		goto errout;
	}

	if (kmm_malloc(0) != NULL || kmm_zalloc(0) != NULL) {
		goto errout;
	}

	mem = (FAR unsigned char *)kmm_malloc(TEST_KMM_ALLOC_SIZE);
	if (mem == NULL || !kmm_heapmember(mem)) {
		goto errout;
	}

	heap = kmm_get_heap(mem);
	heap_index = kmm_get_index_of_heap(mem);
	if (heap == NULL || heap_index < HEAP_START_IDX || heap_index > HEAP_END_IDX ||
		kmm_get_heap_with_index(heap_index) != heap) {
		goto errout;
	}

	memset(mem, TEST_KMM_PATTERN, TEST_KMM_ALLOC_SIZE);

	newmem = (FAR unsigned char *)kmm_realloc(mem, TEST_KMM_REALLOC_SIZE);
	if (newmem == NULL) {
		goto errout;
	}

	mem = newmem;
	if (!kmm_heapmember(mem) || test_kmm_pattern_kept(mem, TEST_KMM_ALLOC_SIZE) != OK) {
		goto errout;
	}

	zmem = (FAR unsigned char *)kmm_zalloc(TEST_KMM_ALLOC_SIZE);
	if (zmem == NULL || !kmm_heapmember(zmem) || test_kmm_is_zero(zmem, TEST_KMM_ALLOC_SIZE) != OK) {
		goto errout;
	}

	cmem = (FAR unsigned char *)kmm_calloc(TEST_KMM_CALLOC_COUNT, sizeof(uint32_t));
	if (cmem == NULL || !kmm_heapmember(cmem) || test_kmm_is_zero(cmem, TEST_KMM_CALLOC_COUNT * sizeof(uint32_t)) != OK) {
		goto errout;
	}

	aligned = (FAR unsigned char *)kmm_memalign(TEST_KMM_ALIGN, TEST_KMM_ALLOC_SIZE);
	if (aligned == NULL || !kmm_heapmember(aligned) || ((uintptr_t)aligned % TEST_KMM_ALIGN) != 0) {
		goto errout;
	}

	ret = OK;

errout:
	if (aligned != NULL) {
		kmm_free(aligned);
	}

	if (cmem != NULL) {
		kmm_free(cmem);
	}

	if (zmem != NULL) {
		kmm_free(zmem);
	}

	if (mem != NULL) {
		kmm_free(mem);
	}

	return ret;

errout_with_heap_lock:
	test_kmm_unlock_heaps(baseheap);
	return ERROR;
}

static int test_kmm_heap_all(void)
{
	int ret;

	ret = test_kmm_heap_negative_index();
	if (ret != OK) {
		return ret;
	}

	ret = test_kmm_heap_zero_index();
	if (ret != OK) {
		return ret;
	}

#if CONFIG_KMM_NHEAPS > 1
	ret = test_kmm_heap_nonzero_index();
	if (ret != OK) {
		return ret;
	}
#endif

	ret = test_kmm_heap_upper_bound_index();
	if (ret != OK) {
		return ret;
	}

#ifdef CONFIG_BUILD_PROTECTED
	ret = test_kmm_heap_user_address();
	if (ret != OK) {
		return ret;
	}
#endif

#if defined(CONFIG_APP_BINARY_SEPARATION) && defined(CONFIG_DEBUG_MM_HEAPINFO)
	ret = test_kmm_alloc_fail_traversal();
	if (ret != OK) {
		return ret;
	}
#endif

	return test_kmm_heap(0);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_kmm(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_KMM_HEAP_TEST:
#ifdef CONFIG_MM_KERNEL_HEAP
		switch (arg) {
		case TEST_KMM_HEAP_ALL:
			ret = test_kmm_heap_all();
			break;
		case TEST_KMM_HEAP_NEGATIVE_INDEX:
			ret = test_kmm_heap_negative_index();
			break;
		case TEST_KMM_HEAP_ZERO_INDEX:
			ret = test_kmm_heap_zero_index();
			break;
		case TEST_KMM_HEAP_NONZERO_INDEX:
			ret = test_kmm_heap_nonzero_index();
			break;
		case TEST_KMM_HEAP_UPPER_BOUND_INDEX:
			ret = test_kmm_heap_upper_bound_index();
			break;
		case TEST_KMM_HEAP_USER_ADDRESS:
			ret = test_kmm_heap_user_address();
			break;
		case TEST_KMM_HEAP_ALLOC_FAIL_TRAVERSAL:
			ret = test_kmm_alloc_fail_traversal();
			break;
		case TEST_KMM_HEAP_ALLOCATIONS:
			ret = test_kmm_heap(arg);
			break;
		default:
			ret = -EINVAL;
			break;
		}
#else
		ret = -ENOSYS;
#endif
		break;
	}

	return ret;
}
