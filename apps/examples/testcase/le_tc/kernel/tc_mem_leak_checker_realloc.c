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

#include <tinyara/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <tinyara/mm/mm.h>

#define MLC_REALLOC_HEAP_SIZE 4096
#define MLC_REALLOC_OLD_SIZE 96

union mlc_realloc_storage_u {
	uintptr_t alignment;
	uint8_t bytes[MLC_REALLOC_HEAP_SIZE];
};

static struct mm_heap_s g_mlc_realloc_heap;
static union mlc_realloc_storage_u g_mlc_realloc_storage;
static bool g_mlc_realloc_initialized;

static bool mlc_requested_size_is(void *allocation, size_t requested)
{
	struct mm_allocnode_s *node;
	size_t actual;

	if (allocation == NULL) {
		return false;
	}
	node = (struct mm_allocnode_s *)((char *)allocation - SIZEOF_MM_ALLOCNODE);
	return mm_allocnode_get_requested_size(node, &actual) && actual == requested;
}

static void mlc_fill(void *allocation)
{
	size_t index;

	for (index = 0; index < MLC_REALLOC_OLD_SIZE; index++) {
		((uint8_t *)allocation)[index] = (uint8_t)(0x31 + index);
	}
}

static bool mlc_data_is(const void *allocation, size_t length)
{
	size_t index;

	for (index = 0; index < length; index++) {
		if (((const uint8_t *)allocation)[index] != (uint8_t)(0x31 + index)) {
			return false;
		}
	}
	return true;
}

static void *mlc_alloc(size_t size)
{
	return mm_malloc(&g_mlc_realloc_heap, size, 0);
}

static void mlc_free(void *allocation)
{
	if (allocation != NULL) {
		mm_free(&g_mlc_realloc_heap, allocation);
	}
}

static bool mlc_realloc_in_place(size_t initial, size_t requested, bool shrink)
{
	void *allocation = mlc_alloc(initial);
	void *guard = mlc_alloc(MLC_REALLOC_OLD_SIZE);
	void *resized;
	bool passed;

	if (allocation == NULL || guard == NULL) {
		mlc_free(guard);
		mlc_free(allocation);
		return false;
	}
	mlc_fill(allocation);
	resized = mm_realloc(&g_mlc_realloc_heap, allocation, requested, 0);
	passed = resized == allocation && mlc_requested_size_is(resized, requested) &&
			mlc_data_is(resized, shrink ? requested : MLC_REALLOC_OLD_SIZE);
	mlc_free(guard);
	mlc_free(resized);
	return passed && mm_check_heap_corruption(&g_mlc_realloc_heap) == OK;
}

static bool mlc_realloc_neighbor(bool release_previous, bool release_next,
		size_t requested, bool expect_both)
{
	void *previous = mlc_alloc(MLC_REALLOC_OLD_SIZE);
	void *allocation = mlc_alloc(MLC_REALLOC_OLD_SIZE);
	void *next = mlc_alloc(MLC_REALLOC_OLD_SIZE);
	void *guard = mlc_alloc(MLC_REALLOC_OLD_SIZE);
	void *old = allocation;
	void *resized = NULL;
	bool passed = false;
	size_t previous_chunk;
	size_t old_chunk;
	size_t next_chunk;
	size_t new_chunk = MM_ALIGN_UP(requested + SIZEOF_MM_ALLOCNODE);

	if (previous == NULL || allocation == NULL || next == NULL || guard == NULL) {
		goto cleanup;
	}
	previous_chunk = ((struct mm_allocnode_s *)((char *)previous -
			SIZEOF_MM_ALLOCNODE))->size;
	old_chunk = ((struct mm_allocnode_s *)((char *)allocation -
			SIZEOF_MM_ALLOCNODE))->size;
	next_chunk = ((struct mm_allocnode_s *)((char *)next -
			SIZEOF_MM_ALLOCNODE))->size;
	mlc_fill(allocation);
	if (release_previous) {
		mlc_free(previous);
		previous = NULL;
	}
	if (release_next) {
		mlc_free(next);
		next = NULL;
	}
	resized = mm_realloc(&g_mlc_realloc_heap, allocation, requested, 0);
	if (resized != NULL) {
		allocation = NULL;
	}
	passed = resized != NULL && mlc_requested_size_is(resized, requested) &&
			mlc_data_is(resized, MLC_REALLOC_OLD_SIZE);
	if (release_previous) {
		passed = passed && (uintptr_t)resized < (uintptr_t)old;
	} else {
		passed = passed && resized == old;
	}
	if (expect_both) {
		passed = passed && release_previous && release_next &&
				new_chunk > old_chunk + previous_chunk &&
				new_chunk > old_chunk + next_chunk &&
				new_chunk <= old_chunk + previous_chunk + next_chunk;
	}

cleanup:
	mlc_free(guard);
	mlc_free(next);
	mlc_free(resized);
	mlc_free(allocation);
	mlc_free(previous);
	return passed && mm_check_heap_corruption(&g_mlc_realloc_heap) == OK;
}

static bool mlc_realloc_move(void)
{
	void *allocation = mlc_alloc(MLC_REALLOC_OLD_SIZE);
	void *blocker = mlc_alloc(MLC_REALLOC_OLD_SIZE);
	void *old = allocation;
	void *resized = NULL;
	bool passed = false;

	if (allocation != NULL && blocker != NULL) {
		mlc_fill(allocation);
		resized = mm_realloc(&g_mlc_realloc_heap, allocation, 512, 0);
		if (resized != NULL) {
			allocation = NULL;
		}
		passed = resized != NULL && resized != old &&
				mlc_requested_size_is(resized, 512) &&
				mlc_data_is(resized, MLC_REALLOC_OLD_SIZE);
	}
	mlc_free(blocker);
	mlc_free(resized);
	mlc_free(allocation);
	return passed && mm_check_heap_corruption(&g_mlc_realloc_heap) == OK;
}

static bool mlc_realloc_failure(void)
{
	void *allocation = mlc_alloc(MLC_REALLOC_OLD_SIZE);
	struct mm_allocnode_s *node;
	uint16_t padding;
	bool passed;

	if (allocation == NULL) {
		return false;
	}
	mlc_fill(allocation);
	node = (struct mm_allocnode_s *)((char *)allocation - SIZEOF_MM_ALLOCNODE);
	padding = node->alloc_padding;
	passed = mm_realloc(&g_mlc_realloc_heap, allocation, SIZE_MAX, 0) == NULL &&
			node->alloc_padding == padding &&
			mlc_requested_size_is(allocation, MLC_REALLOC_OLD_SIZE) &&
			mlc_data_is(allocation, MLC_REALLOC_OLD_SIZE);
	mlc_free(allocation);
	return passed && mm_check_heap_corruption(&g_mlc_realloc_heap) == OK;
}

bool tc_mem_leak_checker_realloc_fixture(void)
{
	bool passed;

	if (!g_mlc_realloc_initialized) {
		if (mm_initialize(&g_mlc_realloc_heap, g_mlc_realloc_storage.bytes,
				MLC_REALLOC_HEAP_SIZE) != OK) {
			return false;
		}
		g_mlc_realloc_initialized = true;
	}
	passed = mlc_realloc_in_place(MLC_REALLOC_OLD_SIZE,
			MLC_REALLOC_OLD_SIZE, false);
	passed = mlc_realloc_in_place(160, MLC_REALLOC_OLD_SIZE, true) && passed;
	passed = mlc_realloc_neighbor(true, false, 160, false) && passed;
	passed = mlc_realloc_neighbor(false, true, 160, false) && passed;
	passed = mlc_realloc_neighbor(true, true, 264, true) && passed;
	passed = mlc_realloc_move() && passed;
	return mlc_realloc_failure() && passed;
}
