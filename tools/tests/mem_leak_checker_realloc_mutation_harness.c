#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mm_realloc_logic.h"

#define HEADER_SIZE 16
#define FREE_NODE_SIZE 32
#define STORAGE_SIZE 320
#define OLD_OFFSET 112
#define MOVE_OFFSET 224

struct fake_heap_s {
	uint8_t bytes[STORAGE_SIZE];
	size_t payload_offset;
	size_t chunk_size;
	uint16_t padding;
};

static void fail_case(const char *scenario)
{
	fprintf(stderr, "failed fake-heap scenario: %s\n", scenario);
	exit(EXIT_FAILURE);
}

static void init_heap(struct fake_heap_s *heap, size_t old_chunk, size_t requested)
{
	size_t index;

	memset(heap->bytes, 0xa5, sizeof(heap->bytes));
	heap->payload_offset = OLD_OFFSET;
	heap->chunk_size = old_chunk;
	if (!mm_realloc_publish_padding(old_chunk, HEADER_SIZE, requested, &heap->padding)) {
		fail_case("initial_padding");
	}
	for (index = 0; index < old_chunk - HEADER_SIZE; index++) {
		heap->bytes[OLD_OFFSET + index] = (uint8_t)(0x20 + index);
	}
}

static bool mutate_heap(struct fake_heap_s *heap, size_t requested,
		size_t previous_size, size_t next_size, bool move_succeeds,
		enum mm_realloc_branch_e *observed_branch)
{
	struct mm_realloc_plan_s plan;
	size_t new_chunk = requested + HEADER_SIZE;
	size_t old_payload = heap->chunk_size - HEADER_SIZE;
	size_t destination = heap->payload_offset;
	size_t published_chunk;
	uint16_t published_padding;

	if (!mm_realloc_plan(heap->chunk_size, new_chunk, previous_size, next_size,
			FREE_NODE_SIZE, &plan)) {
		return false;
	}
	*observed_branch = plan.branch;
	if (plan.branch == MM_REALLOC_BRANCH_MOVE) {
		if (!move_succeeds) {
			return false;
		}
		destination = MOVE_OFFSET;
		published_chunk = new_chunk;
	} else {
		destination -= plan.take_previous;
		published_chunk = plan.final_size;
	}
	if (!mm_realloc_publish_padding(published_chunk, HEADER_SIZE, requested,
			&published_padding)) {
		return false;
	}
	if (destination != heap->payload_offset) {
		mm_realloc_copy(&heap->bytes[destination], &heap->bytes[heap->payload_offset],
				old_payload, requested);
	}
	heap->payload_offset = destination;
	heap->chunk_size = published_chunk;
	heap->padding = published_padding;
	return true;
}

static void expect_branch(const char *scenario, size_t old_chunk, size_t old_request,
		size_t request, size_t previous_size, size_t next_size,
		enum mm_realloc_branch_e expected_branch)
{
	struct fake_heap_s heap;
	enum mm_realloc_branch_e branch;
	size_t original_offset;
	size_t copy_size;
	size_t index;
	uint8_t before_guard;
	uint8_t after_guard;

	init_heap(&heap, old_chunk, old_request);
	original_offset = heap.payload_offset;
	copy_size = mm_realloc_copy_size(old_chunk - HEADER_SIZE, request);
	before_guard = heap.bytes[original_offset - 1];
	after_guard = heap.bytes[original_offset + old_chunk - HEADER_SIZE];
	if (!mutate_heap(&heap, request, previous_size, next_size, true, &branch) ||
			branch != expected_branch ||
			heap.chunk_size - HEADER_SIZE - heap.padding != request) {
		fail_case(scenario);
	}
	for (index = 0; index < copy_size; index++) {
		if (heap.bytes[heap.payload_offset + index] != (uint8_t)(0x20 + index)) {
			fail_case(scenario);
		}
	}
	if (expected_branch == MM_REALLOC_BRANCH_MOVE) {
		if (heap.bytes[MOVE_OFFSET - 1] != 0xa5 ||
				heap.bytes[MOVE_OFFSET + copy_size] != 0xa5) {
			fail_case("move_canaries");
		}
	} else if (expected_branch == MM_REALLOC_BRANCH_PREVIOUS ||
			expected_branch == MM_REALLOC_BRANCH_BOTH) {
		if (heap.bytes[heap.payload_offset - 1] != 0xa5 ||
				heap.bytes[heap.payload_offset + copy_size] == 0xa5) {
			fail_case("overlap_publication_canaries");
		}
	} else if (heap.bytes[original_offset - 1] != before_guard ||
			heap.bytes[original_offset + old_chunk - HEADER_SIZE] != after_guard) {
		fail_case("in_place_canaries");
	}
}

static void expect_failure_preserves_state(void)
{
	struct fake_heap_s heap;
	struct fake_heap_s before;
	enum mm_realloc_branch_e branch;

	init_heap(&heap, 64, 48);
	before = heap;
	if (mutate_heap(&heap, 128, 16, 16, false, &branch) ||
			memcmp(&heap, &before, sizeof(heap)) != 0) {
		fail_case("failure_preserves_all_metadata_and_bytes");
	}
}

int main(void)
{
	expect_branch("same", 64, 48, 48, 0, 0, MM_REALLOC_BRANCH_SAME);
	expect_branch("shrink", 64, 48, 32, 0, 0, MM_REALLOC_BRANCH_SHRINK);
	expect_branch("previous", 64, 48, 64, 48, 0, MM_REALLOC_BRANCH_PREVIOUS);
	expect_branch("next", 64, 48, 64, 0, 48, MM_REALLOC_BRANCH_NEXT);
	expect_branch("both", 64, 48, 112, 48, 48, MM_REALLOC_BRANCH_BOTH);
	expect_branch("move", 64, 48, 96, 16, 16, MM_REALLOC_BRANCH_MOVE);
	expect_failure_preserves_state();
	printf("MLC_QA_MUTATION fake_heap_branches=same,shrink,previous,next,both,move "
			"metadata=PASS canaries=PASS failure_preservation=PASS\n");
	return 0;
}
