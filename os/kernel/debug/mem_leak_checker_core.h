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

#ifndef __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_CORE_H
#define __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mem_leak_checker_budget.h"

#define MLC_EMPTY_SLOT SIZE_MAX

enum mlc_core_result_e {
	MLC_CORE_OK = 0,
	MLC_CORE_INVALID_ARGUMENT = -1,
	MLC_CORE_INVALID_RANGE = -2,
	MLC_CORE_INSUFFICIENT_WORKSPACE = -3,
	MLC_CORE_CORRUPT_INDEX = -4,
	MLC_CORE_NOT_SCANNABLE = -5,
	MLC_CORE_ALIASING_WORKSPACE = -6,
	MLC_CORE_INSUFFICIENT_OUTPUT = -7
};

enum mlc_candidate_state_e {
	MLC_CANDIDATE_ALLOCATED = 0,
	MLC_CANDIDATE_FREED
};

enum mlc_target_kind_e {
	MLC_TARGET_EXACT = 0,
	MLC_TARGET_INTERIOR
};

enum mlc_source_alignment_e {
	MLC_SOURCE_ALIGNED = 0,
	MLC_SOURCE_UNALIGNED
};

/*
 * Todo 7/11 producers must snapshot one record per allocator node while heap
 * ownership is held.  payload_begin is the returned user address,
 * payload_capacity excludes the allocator header, content_size is the exact
 * requested extent, and extent_valid means the allocator padding check passed.
 * Freed or invalid-extent records remain diagnostic identities only: they are
 * never indexed, targeted, or scanned.  Candidate and workspace storage must
 * remain unchanged for the duration of every lookup or scan call.
 */
struct mlc_candidate_s {
	uintptr_t payload_begin;
	size_t content_size;
	size_t payload_capacity;
	size_t candidate_id;
	enum mlc_candidate_state_e state;
	bool extent_valid;
};

struct mlc_candidate_workspace_s {
	size_t *sorted_indices;
	size_t sorted_capacity;
	size_t *exact_slots;
	size_t exact_capacity;
	struct mlc_operation_counters_s *counters;
};

struct mlc_operation_counters_s {
	size_t validation_calls;
	size_t lookup_probes;
	size_t scanned_windows;
};

struct mlc_candidate_index_s {
	const struct mlc_candidate_s *candidates;
	size_t candidate_count;
	const size_t *sorted_indices;
	size_t indexed_count;
	size_t sorted_capacity;
	const size_t *exact_slots;
	size_t exact_capacity;
	struct mlc_operation_counters_s *counters;
	const struct mlc_candidate_workspace_s *workspace;
};

struct mlc_lookup_s {
	size_t candidate_id;
	size_t candidate_index;
	enum mlc_target_kind_e kind;
};

struct mlc_match_s {
	size_t source_offset;
	uintptr_t value;
	size_t candidate_id;
	size_t candidate_index;
	enum mlc_target_kind_e target_kind;
	enum mlc_source_alignment_e alignment;
};

int mlc_candidate_index_build(struct mlc_candidate_index_s *index,
		const struct mlc_candidate_s *candidates, size_t candidate_count,
		const struct mlc_candidate_workspace_s *workspace);
int mlc_candidate_index_validate(const struct mlc_candidate_index_s *index);
int mlc_candidate_lookup(const struct mlc_candidate_index_s *index,
		uintptr_t value, bool *found, struct mlc_lookup_s *lookup);
int mlc_scan_range(const struct mlc_candidate_index_s *index,
		const void *source, size_t source_size, uintptr_t source_begin,
		struct mlc_match_s *matches, size_t match_capacity,
		size_t *match_count);
int mlc_scan_candidate(const struct mlc_candidate_index_s *index,
		size_t candidate_index, const void *source,
		struct mlc_match_s *matches, size_t match_capacity,
		size_t *match_count);

/*
 * The exact table needs at least 2 * indexed_count + 1 slots.  Validation is
 * O(candidate_count squared) exactly once at scan entry.  Each byte window
 * then performs one bounded hash-probe sequence plus binary interval lookup
 * with local bounds and candidate-state checks.  Scans count matches before
 * emitting them, so insufficient output leaves the caller's array and count
 * untouched.  Todo 10 budgets these counters.
 */

#endif
