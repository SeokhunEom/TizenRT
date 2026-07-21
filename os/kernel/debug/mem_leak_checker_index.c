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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mem_leak_checker_core.h"
#include "mem_leak_checker_core_internal.h"

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

struct byte_span_s {
	uintptr_t begin;
	uintptr_t end;
	bool empty;
};

static int mlc_index_budget_take(enum mlc_budget_counter_e counter)
{
	struct mlc_budget_counters_s *budget = mlc_budget_current();
	int ret;

	if (budget == NULL) {
		return 0;
	}
	ret = mlc_budget_chunk_begin(budget, counter, 1,
		mlc_budget_clock_now());
	return ret < 0 ? ret : mlc_budget_chunk_end(budget,
		mlc_budget_clock_now());
}

static int byte_span(const void *pointer, size_t count, size_t element_size,
		struct byte_span_s *span)
{
	size_t bytes;
	uintptr_t begin;

	if (count == 0) {
		span->begin = 0;
		span->end = 0;
		span->empty = true;
		return MLC_CORE_OK;
	}
	if (pointer == NULL || element_size == 0 || count > SIZE_MAX / element_size) {
		return pointer == NULL ? MLC_CORE_INVALID_ARGUMENT : MLC_CORE_INVALID_RANGE;
	}
	bytes = count * element_size;
	begin = (uintptr_t)pointer;
	if (bytes > UINTPTR_MAX || begin > UINTPTR_MAX - (uintptr_t)bytes) {
		return MLC_CORE_INVALID_RANGE;
	}
	span->begin = begin;
	span->end = begin + (uintptr_t)bytes;
	span->empty = false;
	return MLC_CORE_OK;
}

static bool spans_overlap(const struct byte_span_s *left,
		const struct byte_span_s *right)
{
	return !left->empty && !right->empty && left->begin < right->end &&
			right->begin < left->end;
}

static int validate_workspace_spans(struct mlc_candidate_index_s *index,
		const struct mlc_candidate_s *candidates, size_t candidate_count,
		const struct mlc_candidate_workspace_s *workspace)
{
	struct byte_span_s spans[6];
	size_t left;
	int result;

	result = byte_span(workspace, 1, sizeof(*workspace), &spans[0]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(index, 1, sizeof(*index), &spans[1]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(candidates, candidate_count, sizeof(*candidates), &spans[2]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	for (left = 0; left < 3; left++) {
		size_t right;

		for (right = left + 1; right < 3; right++) {
			if (spans_overlap(&spans[left], &spans[right])) {
				return MLC_CORE_ALIASING_WORKSPACE;
			}
		}
	}
	result = byte_span(workspace->sorted_indices, workspace->sorted_capacity,
			sizeof(*workspace->sorted_indices), &spans[3]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(workspace->exact_slots, workspace->exact_capacity,
			sizeof(*workspace->exact_slots), &spans[4]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(workspace->counters, workspace->counters == NULL ? 0 : 1,
			sizeof(*workspace->counters), &spans[5]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	for (left = 0; left < COUNT_OF(spans); left++) {
		size_t right;

		for (right = left + 1; right < COUNT_OF(spans); right++) {
			if (spans_overlap(&spans[left], &spans[right])) {
				return MLC_CORE_ALIASING_WORKSPACE;
			}
		}
	}
	return MLC_CORE_OK;
}

static bool add_overflows(uintptr_t begin, size_t size)
{
	return size > UINTPTR_MAX || begin > UINTPTR_MAX - (uintptr_t)size;
}

static bool state_is_valid(enum mlc_candidate_state_e state)
{
	return state == MLC_CANDIDATE_ALLOCATED || state == MLC_CANDIDATE_FREED;
}

static bool is_targetable(const struct mlc_candidate_s *candidate)
{
	return candidate->state == MLC_CANDIDATE_ALLOCATED && candidate->extent_valid;
}

static size_t exact_slot(uintptr_t value, size_t capacity)
{
	return (size_t)(value % capacity);
}

static bool candidates_overlap(const struct mlc_candidate_s *left,
		const struct mlc_candidate_s *right)
{
	const struct mlc_candidate_s *lower = left;
	const struct mlc_candidate_s *upper = right;

	if (left->payload_begin > right->payload_begin) {
		lower = right;
		upper = left;
	}
	if (lower->payload_begin == upper->payload_begin) {
		return true;
	}
	return lower->payload_begin + lower->payload_capacity > upper->payload_begin;
}

static int validate_inputs(struct mlc_candidate_index_s *index,
		const struct mlc_candidate_s *candidates, size_t candidate_count,
		const struct mlc_candidate_workspace_s *workspace, size_t *indexed_count)
{
	size_t current;
	size_t count = 0;
	int result;

	if ((candidate_count > 0 && candidates == NULL) || workspace == NULL ||
			indexed_count == NULL) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	result = validate_workspace_spans(index, candidates, candidate_count, workspace);
	if (result != MLC_CORE_OK) {
		return result;
	}
	for (current = 0; current < candidate_count; current++) {
		const struct mlc_candidate_s *candidate = &candidates[current];
		size_t comparison;

		if (!state_is_valid(candidate->state)) {
			return MLC_CORE_INVALID_ARGUMENT;
		}
		if (!is_targetable(candidate)) {
			continue;
		}
		if (candidate->content_size > candidate->payload_capacity ||
				add_overflows(candidate->payload_begin, candidate->payload_capacity)) {
			return MLC_CORE_INVALID_RANGE;
		}
		for (comparison = 0; comparison < current; comparison++) {
			if (is_targetable(&candidates[comparison]) &&
					candidates_overlap(candidate, &candidates[comparison])) {
				return MLC_CORE_INVALID_RANGE;
			}
		}
		count++;
	}
	if (count > (SIZE_MAX - 1) / 2 || count > workspace->sorted_capacity ||
			workspace->exact_capacity < count * 2 + 1) {
		return MLC_CORE_INSUFFICIENT_WORKSPACE;
	}
	*indexed_count = count;
	return MLC_CORE_OK;
}

static void sort_indices(const struct mlc_candidate_s *candidates,
		size_t *indices, size_t count)
{
	size_t current;

	for (current = 1; current < count; current++) {
		size_t candidate_index = indices[current];
		size_t insertion = current;

		while (insertion > 0 && candidates[indices[insertion - 1]].payload_begin >
				candidates[candidate_index].payload_begin) {
			indices[insertion] = indices[insertion - 1];
			insertion--;
		}
		indices[insertion] = candidate_index;
	}
}

int mlc_candidate_index_build(struct mlc_candidate_index_s *index,
		const struct mlc_candidate_s *candidates, size_t candidate_count,
		const struct mlc_candidate_workspace_s *workspace)
{
	size_t indexed_count;
	size_t current;
	int result;

	if (index == NULL) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	result = validate_inputs(index, candidates, candidate_count, workspace,
			&indexed_count);
	if (result != MLC_CORE_OK) {
		return result;
	}
	for (current = 0; current < workspace->exact_capacity; current++) {
		if (mlc_index_budget_take(MLC_BUDGET_INDEX_SORT_MOVE) < 0) {
			return MLC_CORE_INSUFFICIENT_WORKSPACE;
		}
		workspace->exact_slots[current] = MLC_EMPTY_SLOT;
	}
	indexed_count = 0;
	for (current = 0; current < candidate_count; current++) {
		if (mlc_index_budget_take(MLC_BUDGET_CANDIDATE) < 0) {
			return MLC_CORE_INSUFFICIENT_WORKSPACE;
		}
		if (is_targetable(&candidates[current])) {
			workspace->sorted_indices[indexed_count++] = current;
		}
	}
	sort_indices(candidates, workspace->sorted_indices, indexed_count);
	for (current = 0; current < indexed_count; current++) {
		size_t candidate_index = workspace->sorted_indices[current];
		size_t slot = exact_slot(candidates[candidate_index].payload_begin,
				workspace->exact_capacity);
		size_t probe;

		for (probe = 0; probe < workspace->exact_capacity &&
				workspace->exact_slots[slot] != MLC_EMPTY_SLOT; probe++) {
			slot = (slot + 1) % workspace->exact_capacity;
		}
		workspace->exact_slots[slot] = candidate_index;
	}
	index->candidates = candidates;
	index->candidate_count = candidate_count;
	index->sorted_indices = workspace->sorted_indices;
	index->indexed_count = indexed_count;
	index->sorted_capacity = workspace->sorted_capacity;
	index->exact_slots = workspace->exact_slots;
	index->exact_capacity = workspace->exact_capacity;
	index->counters = workspace->counters;
	index->workspace = workspace;
	return MLC_CORE_OK;
}

static int validate_order(const struct mlc_candidate_index_s *index)
{
	size_t current;
	size_t targetable_count = 0;

	for (current = 0; current < index->candidate_count; current++) {
		const struct mlc_candidate_s *candidate = &index->candidates[current];

		if (!state_is_valid(candidate->state)) {
			return MLC_CORE_CORRUPT_INDEX;
		}
		if (is_targetable(candidate) &&
				(candidate->content_size > candidate->payload_capacity ||
				 add_overflows(candidate->payload_begin, candidate->payload_capacity))) {
			return MLC_CORE_CORRUPT_INDEX;
		}
		if (is_targetable(candidate)) {
			targetable_count++;
		}
	}
	if (targetable_count != index->indexed_count) {
		return MLC_CORE_CORRUPT_INDEX;
	}
	for (current = 0; current < index->indexed_count; current++) {
		size_t candidate_index = index->sorted_indices[current];

		if (candidate_index >= index->candidate_count ||
				!is_targetable(&index->candidates[candidate_index])) {
			return MLC_CORE_CORRUPT_INDEX;
		}
		if (current > 0) {
			size_t previous_index = index->sorted_indices[current - 1];

			if (index->candidates[previous_index].payload_begin >=
					index->candidates[candidate_index].payload_begin ||
					candidates_overlap(&index->candidates[previous_index],
					&index->candidates[candidate_index])) {
				return MLC_CORE_CORRUPT_INDEX;
			}
		}
	}
	return MLC_CORE_OK;
}

static int validate_hash(const struct mlc_candidate_index_s *index)
{
	size_t occupied = 0;
	size_t current;

	for (current = 0; current < index->exact_capacity; current++) {
		size_t candidate_index = index->exact_slots[current];

		if (candidate_index == MLC_EMPTY_SLOT) {
			continue;
		}
		if (candidate_index >= index->candidate_count ||
				!is_targetable(&index->candidates[candidate_index])) {
			return MLC_CORE_CORRUPT_INDEX;
		}
		occupied++;
	}
	return occupied == index->indexed_count ? MLC_CORE_OK : MLC_CORE_CORRUPT_INDEX;
}

static int find_exact(const struct mlc_candidate_index_s *index, uintptr_t value,
		bool *found, size_t *candidate_index, bool count_probes)
{
	size_t probe;
	size_t slot;

	*found = false;
	if (index->indexed_count == 0) {
		return MLC_CORE_OK;
	}
	slot = exact_slot(value, index->exact_capacity);
	for (probe = 0; probe < index->exact_capacity; probe++) {
		size_t stored_index = index->exact_slots[slot];

		if (count_probes && index->counters != NULL) {
			index->counters->lookup_probes++;
		}
		if (stored_index == MLC_EMPTY_SLOT) {
			return MLC_CORE_OK;
		}
		if (stored_index >= index->candidate_count ||
				!is_targetable(&index->candidates[stored_index]) ||
				index->candidates[stored_index].content_size >
				index->candidates[stored_index].payload_capacity ||
				add_overflows(index->candidates[stored_index].payload_begin,
				index->candidates[stored_index].payload_capacity)) {
			return MLC_CORE_CORRUPT_INDEX;
		}
		if (index->candidates[stored_index].payload_begin == value) {
			*found = true;
			*candidate_index = stored_index;
			return MLC_CORE_OK;
		}
		slot = (slot + 1) % index->exact_capacity;
	}
	return MLC_CORE_OK;
}

int mlc_candidate_index_validate(const struct mlc_candidate_index_s *index)
{
	size_t current;
	int result;

	if (index == NULL || index->indexed_count > index->candidate_count ||
			index->indexed_count > index->sorted_capacity || index->workspace == NULL ||
			index->indexed_count > (SIZE_MAX - 1) / 2 ||
			index->exact_capacity < index->indexed_count * 2 + 1 ||
			(index->candidate_count > 0 && index->candidates == NULL) ||
			(index->exact_capacity > 0 && index->exact_slots == NULL) ||
			(index->indexed_count > 0 && (index->sorted_indices == NULL ||
			 index->exact_slots == NULL || index->exact_capacity == 0))) {
		return MLC_CORE_CORRUPT_INDEX;
	}
	if (index->counters != NULL) {
		index->counters->validation_calls++;
	}
	result = validate_order(index);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = validate_hash(index);
	if (result != MLC_CORE_OK) {
		return result;
	}
	for (current = 0; current < index->indexed_count; current++) {
		bool found;
		size_t candidate_index;
		size_t expected = index->sorted_indices[current];

		result = find_exact(index, index->candidates[expected].payload_begin,
				&found, &candidate_index, false);
		if (result != MLC_CORE_OK || !found || candidate_index != expected) {
			return MLC_CORE_CORRUPT_INDEX;
		}
	}
	return MLC_CORE_OK;
}

int mlc_candidate_lookup_validated(const struct mlc_candidate_index_s *index,
		uintptr_t value, bool *found, struct mlc_lookup_s *lookup)
{
	size_t candidate_index = 0;
	size_t lower = 0;
	size_t upper;
	int result;

	if (found == NULL || lookup == NULL) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	*found = false;
	result = find_exact(index, value, found, &candidate_index, true);
	if (result != MLC_CORE_OK) {
		return result;
	}
	if (*found) {
		lookup->candidate_id = index->candidates[candidate_index].candidate_id;
		lookup->candidate_index = candidate_index;
		lookup->kind = MLC_TARGET_EXACT;
		return MLC_CORE_OK;
	}
	upper = index->indexed_count;
	while (lower < upper) {
		size_t middle = lower + (upper - lower) / 2;
		size_t middle_index = index->sorted_indices[middle];

		if (middle_index >= index->candidate_count ||
				!is_targetable(&index->candidates[middle_index]) ||
				index->candidates[middle_index].content_size >
				index->candidates[middle_index].payload_capacity ||
				add_overflows(index->candidates[middle_index].payload_begin,
				index->candidates[middle_index].payload_capacity)) {
			return MLC_CORE_CORRUPT_INDEX;
		}
		if (index->candidates[middle_index].payload_begin < value) {
			lower = middle + 1;
		} else {
			upper = middle;
		}
	}
	if (lower > 0) {
		const struct mlc_candidate_s *candidate;

		candidate_index = index->sorted_indices[lower - 1];
		if (candidate_index >= index->candidate_count ||
				!is_targetable(&index->candidates[candidate_index]) ||
				index->candidates[candidate_index].content_size >
				index->candidates[candidate_index].payload_capacity ||
				add_overflows(index->candidates[candidate_index].payload_begin,
				index->candidates[candidate_index].payload_capacity)) {
			return MLC_CORE_CORRUPT_INDEX;
		}
		candidate = &index->candidates[candidate_index];
		if (value > candidate->payload_begin &&
				value < candidate->payload_begin + candidate->content_size) {
			*found = true;
			lookup->candidate_id = candidate->candidate_id;
			lookup->candidate_index = candidate_index;
			lookup->kind = MLC_TARGET_INTERIOR;
		}
	}
	return MLC_CORE_OK;
}

int mlc_candidate_lookup(const struct mlc_candidate_index_s *index,
		uintptr_t value, bool *found, struct mlc_lookup_s *lookup)
{
	int result;

	if (found == NULL || lookup == NULL) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	result = mlc_candidate_index_validate(index);
	if (result != MLC_CORE_OK) {
		return result;
	}
	return mlc_candidate_lookup_validated(index, value, found, lookup);
}
