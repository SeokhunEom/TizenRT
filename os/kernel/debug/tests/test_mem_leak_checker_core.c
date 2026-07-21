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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../mem_leak_checker_core.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define LIVE(base, content, capacity, id) \
	{base, content, capacity, id, MLC_CANDIDATE_ALLOCATED, true}
#define FREED(base, capacity, id) \
	{base, 0, capacity, id, MLC_CANDIDATE_FREED, false}
#define INVALID_EXTENT(base, capacity, id) \
	{base, capacity, capacity, id, MLC_CANDIDATE_ALLOCATED, false}

struct log_s {
	struct mlc_match_s entries[128];
	size_t count;
};

static struct mlc_candidate_index_s build(const struct mlc_candidate_s *candidates,
		size_t count, size_t *order, size_t order_count, size_t *slots,
		size_t slot_count)
{
	struct mlc_candidate_workspace_s workspace = {
		order, order_count, slots, slot_count, NULL
	};
	struct mlc_candidate_index_s index;

	assert(mlc_candidate_index_build(&index, candidates, count, &workspace) ==
			MLC_CORE_OK);
	return index;
}

static bool lookup(const struct mlc_candidate_index_s *index, uintptr_t value,
		struct mlc_lookup_s *result)
{
	bool found;

	assert(mlc_candidate_lookup(index, value, &found, result) == MLC_CORE_OK);
	return found;
}

static void put_word(uint8_t *bytes, size_t offset, uintptr_t value)
{
	memcpy(bytes + offset, &value, sizeof(value));
}

static void test_scanner_index(void)
{
	size_t empty_slot;
	struct mlc_candidate_workspace_s empty_workspace = {
		&empty_slot, 0, &empty_slot, 1, NULL
	};
	struct mlc_candidate_index_s empty;
	struct mlc_candidate_s candidates[] = {
		LIVE(0x4000, 8, 8, 40), LIVE(0x1000, 16, 16, 10),
		LIVE(0x2000, 32, 32, 20)
	};
	size_t order[3];
	size_t slots[7];
	struct mlc_candidate_index_s index;
	struct mlc_lookup_s result;
	uint8_t bytes[sizeof(uintptr_t) * 2 + 1];
	struct log_s log = {0};

	assert(mlc_candidate_index_build(&empty, NULL, 0, &empty_workspace) == MLC_CORE_OK);
	assert(!lookup(&empty, 0, &result));
	assert(mlc_scan_range(&empty, bytes, sizeof(uintptr_t), 0x5000,
			log.entries, COUNT(log.entries), &log.count) == MLC_CORE_OK &&
			log.count == 0);
	index = build(candidates, COUNT(candidates), order, COUNT(order), slots,
			COUNT(slots));
	assert(order[0] == 1 && order[1] == 2 && order[2] == 0);
	assert(lookup(&index, 0x1000, &result));
	assert(result.kind == MLC_TARGET_EXACT && result.candidate_id == 10);
	assert(lookup(&index, 0x1001, &result));
	assert(result.kind == MLC_TARGET_INTERIOR && result.candidate_id == 10);
	assert(!lookup(&index, 0x1010, &result));
	assert(!lookup(&index, 0x2020, &result));

	memset(bytes, 0xa5, sizeof(bytes));
	put_word(bytes, 0, 0x1000);
	put_word(bytes, sizeof(bytes) - sizeof(uintptr_t), 0x2001);
	assert(mlc_scan_range(&index, bytes, sizeof(bytes), 0x6000,
			log.entries, COUNT(log.entries), &log.count) == MLC_CORE_OK);
	assert(log.count == 2);
	assert(log.entries[0].source_offset == 0 &&
			log.entries[0].alignment == MLC_SOURCE_ALIGNED &&
			log.entries[0].target_kind == MLC_TARGET_EXACT);
	assert(log.entries[1].source_offset == sizeof(bytes) - sizeof(uintptr_t) &&
			log.entries[1].alignment == MLC_SOURCE_UNALIGNED &&
			log.entries[1].target_kind == MLC_TARGET_INTERIOR);

	memset(bytes, 0, sizeof(bytes));
	log.count = 0;
	put_word(bytes, 2, 0x1000);
	assert(mlc_scan_range(&index, bytes, sizeof(uintptr_t) - 1, 0x7001,
			log.entries, COUNT(log.entries), &log.count) == MLC_CORE_OK &&
			log.count == 0);
	assert(mlc_scan_range(&index, bytes, sizeof(uintptr_t) + 2, 0x7001,
			log.entries, COUNT(log.entries), &log.count) == MLC_CORE_OK &&
			log.count == 1);
	assert(log.entries[0].source_offset == 2 &&
			log.entries[0].alignment == MLC_SOURCE_UNALIGNED);
}

static void test_zero_exact_precedence(void)
{
	struct mlc_candidate_s candidates[] = {
		LIVE(0x2ff0, 16, 16, 3), LIVE(0x3000, 0, 0, 7)
	};
	size_t order[2];
	size_t slots[5];
	struct mlc_candidate_index_s index = build(candidates, COUNT(candidates), order,
			COUNT(order), slots, COUNT(slots));
	struct mlc_lookup_s result;
	struct log_s log = {0};

	assert(lookup(&index, 0x3000, &result));
	assert(result.kind == MLC_TARGET_EXACT && result.candidate_id == 7);
	assert(mlc_scan_candidate(&index, 1, NULL, log.entries,
			COUNT(log.entries), &log.count) == MLC_CORE_OK);
	assert(log.count == 0);
}

static void test_full_capacity_collisions(void)
{
	struct mlc_candidate_s candidates[] = {
		LIVE(0x1008, 1, 1, 1), LIVE(0x1098, 1, 1, 2),
		LIVE(0x1128, 1, 1, 3), LIVE(0x11b8, 1, 1, 4)
	};
	struct {
		uint8_t before[16];
		size_t order[4];
		size_t slots[9];
		uint8_t after[16];
	} guarded;
	struct mlc_operation_counters_s counters = {0};
	struct mlc_candidate_workspace_s workspace = {
		guarded.order, COUNT(guarded.order), guarded.slots,
		COUNT(guarded.slots), &counters
	};
	struct mlc_candidate_index_s index;
	struct mlc_lookup_s result;
	size_t current;

	memset(&guarded, 0x5a, sizeof(guarded));
	assert(mlc_candidate_index_build(&index, candidates, COUNT(candidates),
			&workspace) == MLC_CORE_OK);
	for (current = 0; current < COUNT(candidates); current++) {
		assert(lookup(&index, candidates[current].payload_begin, &result));
		assert(result.candidate_id == current + 1);
	}
	counters.lookup_probes = 0;
	assert(!lookup(&index, 0x5000, &result));
	assert(counters.lookup_probes > 0 && counters.lookup_probes <=
			index.indexed_count + 1);
	for (current = 0; current < COUNT(guarded.before); current++) {
		assert(guarded.before[current] == 0x5a && guarded.after[current] == 0x5a);
	}
}

static void test_invalid_nonmutation(void)
{
	struct mlc_candidate_s candidates[] = {
		LIVE(0x1000, 16, 16, 1), LIVE(0x1008, 8, 8, 2)
	};
	struct mlc_candidate_s candidate_copy[COUNT(candidates)];
	size_t order[] = {11, 22};
	size_t order_copy[COUNT(order)];
	size_t slots[] = {33, 44};
	size_t slots_copy[COUNT(slots)];
	struct mlc_candidate_workspace_s workspace = {
		order, COUNT(order), slots, COUNT(slots), NULL
	};
	struct mlc_candidate_index_s index;
	struct mlc_candidate_index_s index_copy;

	memset(&index, 0x6b, sizeof(index));
	memcpy(&index_copy, &index, sizeof(index));
	memcpy(candidate_copy, candidates, sizeof(candidates));
	memcpy(order_copy, order, sizeof(order));
	memcpy(slots_copy, slots, sizeof(slots));
	assert(mlc_candidate_index_build(&index, candidates, COUNT(candidates),
			&workspace) == MLC_CORE_INVALID_RANGE);
	assert(memcmp(&index, &index_copy, sizeof(index)) == 0);
	assert(memcmp(candidates, candidate_copy, sizeof(candidates)) == 0);
	assert(memcmp(order, order_copy, sizeof(order)) == 0);
	assert(memcmp(slots, slots_copy, sizeof(slots)) == 0);
}

static void test_rejected_states_and_corruption(void)
{
	struct mlc_candidate_s candidates[] = {
		LIVE(0x1000, 8, 16, 1), FREED(0x2000, 16, 2),
		INVALID_EXTENT(0x3000, 16, 3)
	};
	size_t order[3];
	size_t slots[3];
	struct mlc_candidate_index_s index = build(candidates, COUNT(candidates), order,
			COUNT(order), slots, COUNT(slots));
	struct mlc_lookup_s result;
	bool found;
	size_t match_count = 0;
	struct mlc_match_s match;
	uintptr_t excluded;

	assert(index.indexed_count == 1);
	assert(!lookup(&index, 0x0fff, &result));
	assert(!lookup(&index, 0x1008, &result));
	assert(!lookup(&index, 0x100f, &result));
	assert(!lookup(&index, 0x2000, &result));
	assert(!lookup(&index, 0x3000, &result));
	excluded = 0x0fff;
	assert(mlc_scan_range(&index, &excluded, sizeof(excluded), 0x5000,
			&match, 1, &match_count) == MLC_CORE_OK && match_count == 0);
	excluded = 0x1008;
	assert(mlc_scan_range(&index, &excluded, sizeof(excluded), 0x5000,
			&match, 1, &match_count) == MLC_CORE_OK && match_count == 0);
	excluded = 0x2000;
	assert(mlc_scan_range(&index, &excluded, sizeof(excluded), 0x5000,
			&match, 1, &match_count) == MLC_CORE_OK && match_count == 0);
	assert(mlc_scan_candidate(&index, 1, NULL, NULL, 0, &match_count) ==
			MLC_CORE_NOT_SCANNABLE);
	slots[0] = SIZE_MAX - 1;
	assert(mlc_candidate_lookup(&index, 0x1000, &found, &result) ==
			MLC_CORE_CORRUPT_INDEX);
	index = build(candidates, COUNT(candidates), order, COUNT(order), slots,
			COUNT(slots));
	order[0] = SIZE_MAX - 1;
	assert(mlc_candidate_lookup(&index, 0x1000, &found, &result) ==
			MLC_CORE_CORRUPT_INDEX);
}

static void test_scan_operation_bounds(void)
{
	struct mlc_candidate_s candidate[] = {LIVE(0x1000, 8, 8, 1)};
	size_t order[1];
	size_t slots[3];
	struct mlc_operation_counters_s counters = {0};
	struct mlc_candidate_workspace_s workspace = {order, 1, slots, 3, &counters};
	struct mlc_candidate_index_s index;
	uint8_t bytes[1024 + sizeof(uintptr_t) - 1];
	struct log_s log = {0};
	uint8_t match_bytes[64];
	struct mlc_candidate_s zero_candidate[] = {LIVE(0, 8, 8, 2)};
	struct mlc_match_s match_output[sizeof(match_bytes) - sizeof(uintptr_t) + 2];
	struct mlc_match_s match_copy[COUNT(match_output)];
	size_t required = sizeof(match_bytes) - sizeof(uintptr_t) + 1;
	size_t output_count;

	memset(bytes, 0, sizeof(bytes));
	assert(mlc_candidate_index_build(&index, candidate, 1, &workspace) == MLC_CORE_OK);
	assert(mlc_scan_range(&index, bytes, sizeof(bytes), 0x9000,
			log.entries, COUNT(log.entries), &log.count) == MLC_CORE_OK);
	assert(log.count == 0);
	assert(counters.validation_calls == 1);
	assert(counters.scanned_windows == 1024);
	assert(counters.lookup_probes <= counters.scanned_windows *
			(index.indexed_count + 1));
	memset(&counters, 0, sizeof(counters));
	assert(mlc_scan_candidate(&index, 0, bytes, log.entries,
			COUNT(log.entries), &log.count) == MLC_CORE_OK);
	assert(counters.validation_calls == 1);
	assert(counters.scanned_windows == 1);
	memset(&counters, 0, sizeof(counters));
	memset(match_bytes, 0, sizeof(match_bytes));
	assert(mlc_candidate_index_build(&index, zero_candidate, 1, &workspace) ==
			MLC_CORE_OK);
	output_count = 0;
	assert(mlc_scan_range(&index, match_bytes, sizeof(match_bytes), 0xa000,
			match_output, required, &output_count) == MLC_CORE_OK);
	assert(output_count == required);
	assert(counters.validation_calls == 1);
	memset(&counters, 0, sizeof(counters));
	memset(match_output, 0x6b, sizeof(match_output));
	memcpy(match_copy, match_output, sizeof(match_output));
	output_count = 99;
	assert(mlc_scan_range(&index, match_bytes, sizeof(match_bytes), 0xa000,
			match_output, required - 1, &output_count) ==
			MLC_CORE_INSUFFICIENT_OUTPUT);
	assert(output_count == 99);
	assert(memcmp(match_output, match_copy, sizeof(match_output)) == 0);
	assert(counters.validation_calls == 1);
	memset(&counters, 0, sizeof(counters));
	output_count = 0;
	assert(mlc_scan_range(&index, match_bytes, sizeof(match_bytes), 0xa000,
			match_output, required + 1, &output_count) == MLC_CORE_OK);
	assert(output_count == required && counters.validation_calls == 1);
}

static void test_workspace_span_rejection(void)
{
	union shared_u {
		struct mlc_candidate_s candidate;
		size_t words[8];
	} shared;
	union shared_u shared_copy;
	struct mlc_candidate_index_s index;
	struct mlc_candidate_index_s index_copy;
	size_t order[1] = {17};
	size_t order_copy[1];
	struct mlc_candidate_workspace_s workspace;
	size_t single_shared = 23;
	size_t single_shared_copy;
	size_t partial[5] = {1, 2, 3, 4, 5};
	size_t partial_copy[5];
	struct mlc_candidate_s candidate = LIVE(0x2000, 8, 8, 2);
	struct mlc_candidate_s candidate_copy;
	size_t exact[3] = {7, 8, 9};
	size_t exact_copy[3];
	union output_candidate_u {
		struct mlc_candidate_index_s index;
		struct mlc_candidate_s candidate;
	} output_candidate;
	union output_partial_u {
		struct mlc_candidate_index_s index;
		uint8_t bytes[sizeof(struct mlc_candidate_index_s) + sizeof(size_t)];
	} output_partial;
	union output_candidate_u output_candidate_copy;
	union output_partial_u output_partial_copy;
	struct mlc_candidate_workspace_s workspace_copy;

	shared.candidate = (struct mlc_candidate_s)LIVE(0x1000, 8, 8, 1);
	memcpy(&shared_copy, &shared, sizeof(shared));
	memcpy(order_copy, order, sizeof(order));
	memset(&index, 0x6b, sizeof(index));
	memcpy(&index_copy, &index, sizeof(index));
	workspace = (struct mlc_candidate_workspace_s){
		order, 1, (size_t *)&shared.candidate, 1, NULL
	};
	assert(mlc_candidate_index_build(&index, &shared.candidate, 1, &workspace) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(&shared, &shared_copy, sizeof(shared)) == 0);
	assert(memcmp(order, order_copy, sizeof(order)) == 0);
	assert(memcmp(&index, &index_copy, sizeof(index)) == 0);

	output_candidate.candidate = (struct mlc_candidate_s)LIVE(0x3000, 8, 8, 3);
	memcpy(&output_candidate_copy, &output_candidate, sizeof(output_candidate));
	memcpy(order_copy, order, sizeof(order));
	memcpy(exact_copy, exact, sizeof(exact));
	workspace = (struct mlc_candidate_workspace_s){order, 1, exact, 3, NULL};
	assert(mlc_candidate_index_build(&output_candidate.index,
			&output_candidate.candidate, 1, &workspace) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(&output_candidate, &output_candidate_copy,
			sizeof(output_candidate)) == 0);
	assert(memcmp(order, order_copy, sizeof(order)) == 0);
	assert(memcmp(exact, exact_copy, sizeof(exact)) == 0);

	memset(&output_partial, 0x4d, sizeof(output_partial));
	memcpy(&output_partial_copy, &output_partial, sizeof(output_partial));
	memcpy(&candidate_copy, &candidate, sizeof(candidate));
	memcpy(exact_copy, exact, sizeof(exact));
	workspace = (struct mlc_candidate_workspace_s){
		(size_t *)(void *)(output_partial.bytes + sizeof(size_t)), 1,
		exact, 3, NULL
	};
	assert(mlc_candidate_index_build(&output_partial.index, &candidate, 1,
			&workspace) == MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(&output_partial, &output_partial_copy,
			sizeof(output_partial)) == 0);
	assert(memcmp(&candidate, &candidate_copy, sizeof(candidate)) == 0);

	workspace = (struct mlc_candidate_workspace_s){order, 1, exact, 3, NULL};
	memcpy(&workspace_copy, &workspace, sizeof(workspace));
	memcpy(&index_copy, &index, sizeof(index));
	assert(mlc_candidate_index_build((struct mlc_candidate_index_s *)(void *)&workspace,
			&candidate, 1, &workspace) == MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(&workspace, &workspace_copy, sizeof(workspace)) == 0);
	assert(memcmp(&index, &index_copy, sizeof(index)) == 0);

	workspace = (struct mlc_candidate_workspace_s){order, 1, exact, 3, NULL};
	memcpy(&workspace_copy, &workspace, sizeof(workspace));
	assert(mlc_candidate_index_build(&index,
			(const struct mlc_candidate_s *)(const void *)&workspace, 1,
			&workspace) == MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(&workspace, &workspace_copy, sizeof(workspace)) == 0);
	assert(memcmp(&index, &index_copy, sizeof(index)) == 0);

	workspace = (struct mlc_candidate_workspace_s){
		(size_t *)(void *)((uint8_t *)&workspace + sizeof(size_t)), 1,
		exact, 3, NULL
	};
	memcpy(&workspace_copy, &workspace, sizeof(workspace));
	assert(mlc_candidate_index_build(&index, &candidate, 1, &workspace) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(&workspace, &workspace_copy, sizeof(workspace)) == 0);

	workspace = (struct mlc_candidate_workspace_s){
		order, 1, (size_t *)(void *)&workspace, 3, NULL
	};
	memcpy(&workspace_copy, &workspace, sizeof(workspace));
	assert(mlc_candidate_index_build(&index, &candidate, 1, &workspace) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(&workspace, &workspace_copy, sizeof(workspace)) == 0);

	workspace = (struct mlc_candidate_workspace_s){
		order, 1, exact, 3,
		(struct mlc_operation_counters_s *)(void *)&workspace
	};
	memcpy(&workspace_copy, &workspace, sizeof(workspace));
	assert(mlc_candidate_index_build(&index, &candidate, 1, &workspace) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(&workspace, &workspace_copy, sizeof(workspace)) == 0);
	assert(memcmp(exact, exact_copy, sizeof(exact)) == 0);

	memset(&index, 0x6b, sizeof(index));
	memcpy(&index_copy, &index, sizeof(index));
	memcpy(order_copy, order, sizeof(order));
	memcpy(&candidate_copy, &candidate, sizeof(candidate));
	workspace = (struct mlc_candidate_workspace_s){
		order, 1, (size_t *)(void *)&index, 3, NULL
	};
	assert(mlc_candidate_index_build(&index, &candidate, 1, &workspace) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(&index, &index_copy, sizeof(index)) == 0);
	assert(memcmp(order, order_copy, sizeof(order)) == 0);
	assert(memcmp(&candidate, &candidate_copy, sizeof(candidate)) == 0);

	workspace = (struct mlc_candidate_workspace_s){
		order, 1, exact, 3, (struct mlc_operation_counters_s *)(void *)&index
	};
	assert(mlc_candidate_index_build(&index, &candidate, 1, &workspace) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(&index, &index_copy, sizeof(index)) == 0);
	assert(memcmp(order, order_copy, sizeof(order)) == 0);
	assert(memcmp(&candidate, &candidate_copy, sizeof(candidate)) == 0);

	single_shared_copy = single_shared;
	workspace = (struct mlc_candidate_workspace_s){
		&single_shared, 1, &single_shared, 1, NULL
	};
	assert(mlc_candidate_index_build(&index, &candidate, 1, &workspace) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(single_shared == single_shared_copy);

	memcpy(partial_copy, partial, sizeof(partial));
	workspace = (struct mlc_candidate_workspace_s){partial, 2, partial + 1, 3, NULL};
	assert(mlc_candidate_index_build(&index, &candidate, 1, &workspace) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(memcmp(partial, partial_copy, sizeof(partial)) == 0);
	workspace = (struct mlc_candidate_workspace_s){
		order, 1, exact, 3, (struct mlc_operation_counters_s *)&exact[1]
	};
	assert(mlc_candidate_index_build(&index, &candidate, 1, &workspace) ==
			MLC_CORE_ALIASING_WORKSPACE);

	workspace = (struct mlc_candidate_workspace_s){
		order, 1, (size_t *)(UINTPTR_MAX - sizeof(size_t) + 2), 3, NULL
	};
	assert(mlc_candidate_index_build(&index, &candidate, 1, &workspace) ==
			MLC_CORE_INVALID_RANGE);
	workspace = (struct mlc_candidate_workspace_s){order, 1, exact, 3, NULL};
	assert(mlc_candidate_index_build(&index,
			(const struct mlc_candidate_s *)(uintptr_t)0x1000,
			SIZE_MAX / sizeof(struct mlc_candidate_s) + 1, &workspace) ==
			MLC_CORE_INVALID_RANGE);
}

static void test_match_output_span_rejection(void)
{
	struct mlc_candidate_s candidate[] = {LIVE(0x1000, 8, 8, 1)};
	struct mlc_candidate_s candidate_copy[1];
	size_t order[1];
	size_t order_copy[1];
	size_t slots[3];
	size_t slots_copy[3];
	struct mlc_operation_counters_s counters = {0};
	struct mlc_operation_counters_s counters_copy;
	struct mlc_candidate_workspace_s workspace = {order, 1, slots, 3, &counters};
	struct mlc_candidate_workspace_s workspace_copy;
	struct mlc_candidate_index_s index;
	struct mlc_candidate_index_s index_copy;
	struct mlc_match_s match;
	uintptr_t word = 0;
	size_t match_count = 71;

	assert(mlc_candidate_index_build(&index, candidate, 1, &workspace) == MLC_CORE_OK);
	memcpy(candidate_copy, candidate, sizeof(candidate));
	memcpy(order_copy, order, sizeof(order));
	memcpy(slots_copy, slots, sizeof(slots));
	memcpy(&counters_copy, &counters, sizeof(counters));
	memcpy(&workspace_copy, &workspace, sizeof(workspace));
	memcpy(&index_copy, &index, sizeof(index));
	assert(mlc_scan_range(&index, &word, sizeof(word), 0x8000,
			(struct mlc_match_s *)(void *)candidate, 1, &match_count) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(mlc_scan_range(&index, &word, sizeof(word), 0x8000,
			(struct mlc_match_s *)(void *)&index, 1, &match_count) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(mlc_scan_range(&index, &word, sizeof(word), 0x8000,
			(struct mlc_match_s *)(void *)&workspace, 1, &match_count) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(mlc_scan_range(&index, &word, sizeof(word), 0x8000,
			(struct mlc_match_s *)(void *)order, 1, &match_count) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(mlc_scan_range(&index, &word, sizeof(word), 0x8000,
			(struct mlc_match_s *)(void *)slots, 1, &match_count) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(mlc_scan_range(&index, &word, sizeof(word), 0x8000,
			(struct mlc_match_s *)(void *)&counters, 1, &match_count) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(mlc_scan_range(&index, &word, sizeof(word), 0x8000,
			&match, 1, (size_t *)(void *)&counters) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(match_count == 71);
	assert(memcmp(candidate, candidate_copy, sizeof(candidate)) == 0);
	assert(memcmp(order, order_copy, sizeof(order)) == 0);
	assert(memcmp(slots, slots_copy, sizeof(slots)) == 0);
	assert(memcmp(&counters, &counters_copy, sizeof(counters)) == 0);
	assert(memcmp(&workspace, &workspace_copy, sizeof(workspace)) == 0);
	assert(memcmp(&index, &index_copy, sizeof(index)) == 0);
}

static void test_invalid_ranges(void)
{
	struct mlc_candidate_s overflow[] = {LIVE(UINTPTR_MAX - 3, 1, 8, 1)};
	struct mlc_candidate_s too_large[] = {LIVE(0x1000, 9, 8, 1)};
	struct mlc_candidate_s valid[] = {LIVE(0x1000, 8, 8, 1)};
	size_t order[1] = {9};
	size_t slots[3] = {9, 9, 9};
	struct mlc_candidate_workspace_s workspace = {order, 1, slots, 3, NULL};
	struct mlc_candidate_index_s index;
	struct mlc_candidate_index_s forged = {
		NULL, 0, NULL, 0, 0, NULL, 1, NULL, NULL
	};

	assert(mlc_candidate_index_validate(&forged) == MLC_CORE_CORRUPT_INDEX);
	assert(mlc_candidate_index_build(&index, overflow, 1, &workspace) ==
			MLC_CORE_INVALID_RANGE);
	assert(mlc_candidate_index_build(&index, too_large, 1, &workspace) ==
			MLC_CORE_INVALID_RANGE);
	workspace.sorted_capacity = 0;
	assert(mlc_candidate_index_build(&index, valid, 1, &workspace) ==
			MLC_CORE_INSUFFICIENT_WORKSPACE);
	assert(order[0] == 9 && slots[0] == 9);
}

static void test_invalid_scan_canaries(void)
{
	struct guarded_s {
		uint8_t before[16];
		uint8_t source[sizeof(uintptr_t)];
		uint8_t after[16];
	} guarded;
	struct mlc_candidate_s candidate[] = {LIVE(0x1000, 8, 8, 1)};
	size_t order[1];
	size_t slots[3];
	struct mlc_candidate_index_s index = build(candidate, 1, order, 1, slots, 3);
	struct log_s log = {0};
	size_t current;

	memset(&guarded, 0x5a, sizeof(guarded));
	put_word(guarded.source, 0, 0x1008);
	assert(mlc_scan_range(&index, guarded.source, sizeof(guarded.source),
			UINTPTR_MAX - sizeof(guarded.source) + 2, log.entries,
			COUNT(log.entries), &log.count) ==
			MLC_CORE_INVALID_RANGE);
	assert(log.count == 0);
	for (current = 0; current < COUNT(guarded.before); current++) {
		assert(guarded.before[current] == 0x5a && guarded.after[current] == 0x5a);
	}
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		return 64;
	}
	if (strcmp(argv[1], "mlc_scanner_index") == 0) {
		test_scanner_index();
		test_full_capacity_collisions();
		test_scan_operation_bounds();
		puts("MLC_HOST fixture=mlc_scanner_index status=PASS");
		return 0;
	}
	if (strcmp(argv[1], "mlc_zero_exact_precedence") == 0) {
		test_zero_exact_precedence();
		puts("MLC_HOST fixture=mlc_zero_exact_precedence status=PASS");
		return 0;
	}
	if (strcmp(argv[1], "mlc_scanner_invalid_ranges") == 0) {
		test_invalid_nonmutation();
		test_rejected_states_and_corruption();
		test_invalid_ranges();
		test_invalid_scan_canaries();
		test_workspace_span_rejection();
		test_match_output_span_rejection();
		puts("MLC_HOST fixture=mlc_scanner_invalid_ranges status=PASS matches=atomic canaries=intact");
		return 0;
	}
	return 64;
}
