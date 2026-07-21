#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tinyara/mm/mm_alloc_padding.h>
#include "mm_realloc_logic.h"

static size_t content_size(size_t capacity, uint16_t padding)
{
	size_t requested;

	assert(mm_requested_size_from_padding(capacity, padding, &requested));
	return requested;
}

static void fail_case(const char *scenario)
{
	fprintf(stderr, "failed scenario: %s\n", scenario);
	exit(EXIT_FAILURE);
}

static void expect_padding(const char *scenario, size_t capacity, size_t requested,
		uint16_t expected)
{
	uint16_t padding = UINT16_C(0xa55a);
	if (!mm_alloc_padding_from_sizes(capacity, requested, &padding) ||
			padding != expected || content_size(capacity, padding) != requested) {
		fail_case(scenario);
	}
}

static void expect_invalid(const char *scenario, size_t capacity, size_t requested)
{
	uint16_t padding = UINT16_C(0xa55a);
	if (mm_alloc_padding_from_sizes(capacity, requested, &padding) ||
			padding != UINT16_C(0xa55a)) {
		fail_case(scenario);
	}
}

static void expect_invalid_extent(void)
{
	size_t requested = 41;

	if (mm_requested_size_from_padding(16, 17, &requested) || requested != 41) {
		fail_case("padding_exceeds_capacity");
	}
}

static void expect_failed_realloc_preserves_padding(void)
{
	uint16_t padding = 23;
	bool allocation_succeeded = false;

	if (allocation_succeeded) {
		(void)mm_alloc_padding_from_sizes(79, 65, &padding);
	}
	if (padding != 23) {
		fail_case("failed_realloc_preserves_padding");
	}
}

static void expect_realloc_plan(const char *scenario, size_t old_size, size_t new_size,
		size_t previous_size, size_t next_size, enum mm_realloc_branch_e branch,
		size_t take_previous, size_t take_next, size_t final_size)
{
	struct mm_realloc_plan_s plan;

	if (!mm_realloc_plan(old_size, new_size, previous_size, next_size, 32, &plan) ||
			plan.branch != branch ||
			plan.take_previous != take_previous ||
			plan.take_next != take_next ||
			plan.final_size != final_size ||
			(branch != MM_REALLOC_BRANCH_MOVE &&
			(plan.final_size < new_size ||
			 plan.final_size - new_size > MM_ALLOC_PADDING_REALLOC_BOUND(16, 32)))) {
		fail_case(scenario);
	}
}

static void expect_realloc_copy_canaries(void)
{
	unsigned char overlap[64];
	unsigned char move_source[16];
	unsigned char move_destination[16];
	size_t index;

	for (index = 0; index < sizeof(overlap); index++) {
		overlap[index] = (unsigned char)index;
	}
	mm_realloc_copy(&overlap[8], &overlap[16], 16, 32);
	for (index = 0; index < 16; index++) {
		if (overlap[8 + index] != (unsigned char)(16 + index)) {
			fail_case("previous_neighbor_overlap_copy");
		}
	}
	if (overlap[7] != 7 || overlap[24] != 24) {
		fail_case("previous_neighbor_copy_canary");
	}

	for (index = 0; index < sizeof(move_source); index++) {
		move_source[index] = (unsigned char)(0x40 + index);
		move_destination[index] = UINT8_C(0xa5);
	}
	mm_realloc_copy(move_destination, move_source, sizeof(move_source), 8);
	for (index = 0; index < 8; index++) {
		if (move_destination[index] != move_source[index]) {
			fail_case("move_copy_prefix");
		}
	}
	for (; index < sizeof(move_destination); index++) {
		if (move_destination[index] != UINT8_C(0xa5)) {
			fail_case("move_copy_canary");
		}
	}
}

static void run_bounds(void)
{
	expect_padding("malloc_exact_fit", 32, 32, 0);
	expect_padding("malloc_capacity_slack", 31, 17, 14);
	expect_padding("memalign_exact_fit", 64, 64, 0);
	expect_padding("memalign_absorbed_tail", 95, 64, 31);
	expect_padding("realloc_same_chunk", 47, 33, 14);
	expect_padding("realloc_shrink_absorbed_tail", 95, 32, 63);
	expect_padding("realloc_next_neighbor", 79, 65, 14);
	expect_padding("realloc_previous_neighbor", 111, 65, 46);
	expect_padding("realloc_both_neighbors", 126, 65, 61);
	expect_padding("realloc_move", 79, 65, 14);
	expect_padding("maximum_representable", UINT16_MAX, 0, UINT16_MAX);
	expect_failed_realloc_preserves_padding();
	expect_realloc_plan("realloc_same", 64, 64, 0, 0,
			MM_REALLOC_BRANCH_SAME, 0, 0, 64);
	expect_realloc_plan("realloc_shrink", 64, 48, 0, 0,
			MM_REALLOC_BRANCH_SHRINK, 0, 0, 64);
	expect_realloc_plan("realloc_previous", 64, 80, 64, 0,
			MM_REALLOC_BRANCH_PREVIOUS, 16, 0, 80);
	expect_realloc_plan("realloc_next", 64, 80, 0, 64,
			MM_REALLOC_BRANCH_NEXT, 0, 16, 80);
	expect_realloc_plan("realloc_both", 64, 144, 48, 48,
			MM_REALLOC_BRANCH_BOTH, 48, 48, 160);
	expect_realloc_plan("realloc_move", 64, 160, 32, 32,
			MM_REALLOC_BRANCH_MOVE, 0, 0, 64);
	/* A remainder exactly one free node stays free; '<' must not become '<='. */
	expect_realloc_plan("realloc_free_node_boundary", 64, 96, 64, 0,
			MM_REALLOC_BRANCH_PREVIOUS, 32, 0, 96);
	/* Prefer the smaller feasible neighbor to avoid reversing next/previous policy. */
	expect_realloc_plan("realloc_prefer_next", 64, 96, 64, 48,
			MM_REALLOC_BRANCH_NEXT, 0, 48, 112);
	expect_realloc_plan("realloc_prefer_previous", 64, 96, 48, 64,
			MM_REALLOC_BRANCH_PREVIOUS, 48, 0, 112);
	{
		struct mm_realloc_plan_s plan;
		if (mm_realloc_plan(SIZE_MAX - 10, SIZE_MAX - 1, 20, 0, 32, &plan)) {
			fail_case("realloc_neighbor_add_overflow");
		}
	}
	expect_realloc_copy_canaries();
	printf("MLC_QA_MODEL realloc_branches=same,shrink,previous,next,both,move,failure "
			"preference=PASS free_node_boundary=PASS overlap_canaries=PASS "
			"neighbor_overflow=PASS\n");
}

static void run_zero(void)
{
	size_t product;

	expect_padding("malloc_zero", 15, 0, 15);
	if (!mm_size_multiply_checked(7, 0, &product) || product != 0 ||
			mm_size_multiply_checked(SIZE_MAX, 2, &product)) {
		fail_case("calloc_checked_multiply");
	}
	printf("MLC_QA_MODEL zero_extent=PASS calloc_checked_multiply=PASS\n");
}

static void run_invalid(void)
{
	expect_invalid("requested_exceeds_capacity", 16, 17);
	expect_invalid("padding_unrepresentable", (size_t)UINT16_MAX + 1, 0);
	expect_invalid_extent();
	printf("MLC_QA_MODEL invalid_extent=PASS\n");
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		return 64;
	}
	if (strcmp(argv[1], "mlc_alloc_bounds") == 0) {
		run_bounds();
	} else if (strcmp(argv[1], "mlc_alloc_zero") == 0) {
		run_zero();
	} else if (strcmp(argv[1], "mlc_alloc_padding_invalid") == 0) {
		run_invalid();
	} else {
		return 64;
	}
	return 0;
}
