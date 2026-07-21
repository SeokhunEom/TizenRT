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
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "mem_leak_checker_candidates_internal.h"

#define MLC_CANDIDATE_LOOKUP_CONFLICT (SIZE_MAX - 1)
#define MLC_CANDIDATE_LOOKUP_BUDGET (SIZE_MAX - 2)

static int mlc_candidate_budget_take(
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		enum mlc_budget_counter_e counter)
{
	(void)workspace;
	struct mlc_budget_counters_s *budget = mlc_budget_current();
	int ret;
	uint64_t now = mlc_budget_clock_now();
	if (budget == NULL) {
		return 0;
	}

	ret = mlc_budget_chunk_begin(budget, counter, 1, now);
	if (ret < 0) {
		return ret;
	}
	return mlc_budget_chunk_end(budget, now);
}

#define MLC_PRECEDING_SIZE(node) ((node)->preceding & ~MM_ALLOC_BIT)

static int mlc_append_allocation(
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t *allocated, const struct mm_allocnode_s *node)
{
	struct mlc_candidate_s *candidate;
	size_t requested;
	size_t capacity;

	if (*allocated >= workspace->candidate_capacity) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	if (mlc_candidate_budget_take(workspace, MLC_BUDGET_ALLOCATED_NODE) < 0 ||
		mlc_candidate_budget_take(workspace, MLC_BUDGET_CANDIDATE) < 0) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	capacity = node->size - SIZEOF_MM_ALLOCNODE;
	if (!mm_allocnode_get_requested_size(node, &requested) ||
		requested > capacity) {
		return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
	}
	if (mlc_budget_current() != NULL &&
		mlc_budget_add_requested_bytes(mlc_budget_current(), requested) < 0) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	candidate = &workspace->candidates[*allocated];
	candidate->payload_begin = (uintptr_t)node + SIZEOF_MM_ALLOCNODE;
	candidate->content_size = requested;
	candidate->payload_capacity = capacity;
	candidate->candidate_id = *allocated;
	candidate->state = MLC_CANDIDATE_ALLOCATED;
	candidate->extent_valid = true;
	(*allocated)++;
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

static int mlc_collect_heap(const struct mm_heap_s *heap,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t *allocated)
{
	size_t region_count;
	size_t region;

	if (heap == NULL || heap->mm_holder != getpid() ||
		heap->mm_counts_held != 1) {
		return MLC_CANDIDATE_SNAPSHOT_CONTENTION;
	}
#if CONFIG_KMM_REGIONS > 1
	region_count = (size_t)heap->mm_nregions;
	if (region_count == 0 || region_count > CONFIG_KMM_REGIONS) {
		return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
	}
#else
	region_count = 1;
#endif
	for (region = 0; region < region_count; region++) {
		const struct mm_allocnode_s *start = heap->mm_heapstart[region];
		const struct mm_allocnode_s *end = heap->mm_heapend[region];
		const struct mm_allocnode_s *node;
		size_t previous;

		if (start == NULL || end == NULL || (uintptr_t)start >= (uintptr_t)end ||
			start->size != SIZEOF_MM_ALLOCNODE ||
			(start->preceding & MM_ALLOC_BIT) == 0) {
			return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
		}
		node = start;
		previous = node->size;
		while (node != end) {
			uintptr_t address = (uintptr_t)node;
			const struct mm_allocnode_s *next;
			if (mlc_candidate_budget_take(workspace, MLC_BUDGET_HEAP_NODE) < 0 ||
				mlc_candidate_budget_take(workspace, MLC_BUDGET_LINK_VALIDATE) < 0) {
				return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			}

			if (node->size < SIZEOF_MM_ALLOCNODE ||
				node->size > UINTPTR_MAX - address) {
				return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
			}
			next = (const struct mm_allocnode_s *)(address + node->size);
			if ((uintptr_t)next <= address || (uintptr_t)next > (uintptr_t)end ||
				MLC_PRECEDING_SIZE(next) != previous) {
				return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
			}
			node = next;
			if (node != end) {
				int status;

				if ((node->preceding & MM_ALLOC_BIT) != 0) {
					status = mlc_append_allocation(workspace, allocated, node);
					if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
						return status;
					}
				}
				previous = node->size;
			}
		}
		if (end->size != SIZEOF_MM_ALLOCNODE ||
			(end->preceding & MM_ALLOC_BIT) == 0) {
			return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
		}
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

size_t mlc_candidate_find_containing_allocation(
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t allocated, const struct mlc_address_range_s *range)
{
	size_t found = SIZE_MAX;
	size_t index;

	for (index = 0; index < allocated; index++) {
		if (mlc_candidate_budget_take(workspace, MLC_BUDGET_EXCLUSION) < 0) {
			return MLC_CANDIDATE_LOOKUP_BUDGET;
		}
		struct mlc_address_range_s content = {
			workspace->candidates[index].payload_begin,
			workspace->candidates[index].content_size
		};

		if (mlc_candidate_range_contains(&content, range)) {
			if (found != SIZE_MAX) {
				return MLC_CANDIDATE_LOOKUP_CONFLICT;
			}
			found = index;
		} else if (mlc_candidate_ranges_overlap(&content, range)) {
			return MLC_CANDIDATE_LOOKUP_CONFLICT;
		}
	}
	return found;
}

int mlc_candidate_mark_exclusion(
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t allocated, size_t allocation_index, enum mlc_exclusion_kind_e kind,
		size_t *excluded)
{
	size_t index;

	if ((int)kind < 0 || kind >= MLC_EXCLUDE_KIND_COUNT) {
		return MLC_CANDIDATE_SNAPSHOT_INVALID;
	}
	for (index = 0; index < *excluded; index++) {
		if (mlc_candidate_budget_take(workspace, MLC_BUDGET_DEDUP) < 0) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		if (workspace->exclusions[index].allocation.candidate_id ==
			allocation_index) {
			workspace->exclusions[index].kind_mask |= 1u << kind;
			return MLC_CANDIDATE_SNAPSHOT_OK;
		}
	}
	if (allocation_index >= allocated ||
		*excluded >= workspace->exclusion_capacity) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	workspace->exclusions[*excluded].allocation =
		workspace->candidates[allocation_index];
	workspace->exclusions[*excluded].kind_mask = 1u << kind;
	(*excluded)++;
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

static int mlc_collect_exclusions(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t allocated, size_t *excluded)
{
	size_t index;

	for (index = 0; index < request->exclusion_count; index++) {
		const struct mlc_exclusion_range_s *input = &request->exclusions[index];
		if (mlc_candidate_budget_take(workspace, MLC_BUDGET_EXCLUSION) < 0) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		size_t containing;
		uintptr_t end;
		int status;

		if ((int)input->kind < 0 || input->kind >= MLC_EXCLUDE_KIND_COUNT ||
			!mlc_candidate_range_end(&input->range, &end)) {
			return MLC_CANDIDATE_SNAPSHOT_INVALID;
		}
		containing = mlc_candidate_find_containing_allocation(workspace, allocated,
			&input->range);
		if (containing == MLC_CANDIDATE_LOOKUP_BUDGET) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		if (containing == MLC_CANDIDATE_LOOKUP_CONFLICT) {
			return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
		if (containing == SIZE_MAX) {
			continue;
		}
		status = mlc_candidate_mark_exclusion(workspace, allocated, containing,
			input->kind, excluded);
		if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
			return status;
		}
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

static int mlc_compact_candidates(
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t allocated, size_t excluded, size_t *candidate_count)
{
	size_t source;

	*candidate_count = 0;
	for (source = 0; source < allocated; source++) {
		size_t exclusion;
		bool is_excluded = false;

		for (exclusion = 0; exclusion < excluded; exclusion++) {
			if (mlc_candidate_budget_take(workspace, MLC_BUDGET_DEDUP) < 0) {
				return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			}
			if (workspace->exclusions[exclusion].allocation.candidate_id ==
				source) {
				is_excluded = true;
				break;
			}
		}
		if (!is_excluded) {
			workspace->candidates[*candidate_count] =
				workspace->candidates[source];
			(*candidate_count)++;
		}
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

int mlc_candidate_snapshot_collect(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		struct mlc_candidate_snapshot_result_s *result)
{
	size_t allocated = 0;
	size_t excluded = 0;
	size_t root_count = 0;
	size_t candidate_count;
	size_t heap_index;
	int status;

	status = mlc_candidate_validate_request(request, workspace, result);
	if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
		return status;
	}
	status = mlc_candidate_validate_heap_regions(request);
	if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
		return status;
	}
	status = mlc_candidate_validate_storage(request, workspace, result);
	if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
		return status;
	}
	status = mlc_candidate_validate_provenance(request);
	if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
		return status;
	}
	status = mlc_candidate_preflight(request, workspace);
	if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
		return status;
	}
	for (heap_index = 0; heap_index < request->heap_count; heap_index++) {
		if (mlc_candidate_budget_take(workspace, MLC_BUDGET_HEAP_REVALIDATE) < 0) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		if (heap_index > 0 && (uintptr_t)request->heaps[heap_index - 1] >=
			(uintptr_t)request->heaps[heap_index]) {
			return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
		}
		status = mlc_collect_heap(request->heaps[heap_index], workspace,
			&allocated);
		if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
			return status;
		}
	}
	status = mlc_collect_exclusions(request, workspace, allocated, &excluded);
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_candidate_collect_mappings(request, workspace, allocated, &excluded,
			&root_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_candidate_collect_roots(request, workspace, &root_count);
	}
	if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
		return status;
	}
	status = mlc_compact_candidates(workspace, allocated, excluded,
		&candidate_count);
	if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
		return status;
	}
	result->allocated_count = allocated;
	result->candidate_count = candidate_count;
	result->exclusion_count = excluded;
	result->root_count = root_count;
	return allocated == candidate_count + excluded ?
		MLC_CANDIDATE_SNAPSHOT_OK : MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
}
