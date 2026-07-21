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
#include <unistd.h>

#include "mem_leak_checker_candidates_internal.h"

#define MLC_PRECEDING_SIZE(node) ((node)->preceding & ~MM_ALLOC_BIT)

static int mlc_preflight_budget(
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		enum mlc_budget_counter_e counter)
{
	(void)workspace;
	struct mlc_budget_counters_s *budget = mlc_budget_current();
	uint64_t now = mlc_budget_clock_now();
	if (budget == NULL) {
		return 0;
	}
	int ret = mlc_budget_chunk_begin(budget, counter, 1, now);

	return ret < 0 ? ret : mlc_budget_chunk_end(budget,
		now);
}

struct mlc_preflight_scan_s {
	const struct mlc_address_range_s *range;
	size_t allocated;
	size_t matches;
	size_t conflicts;
	bool exact;
};

static int mlc_preflight_node(const struct mm_allocnode_s *node,
		struct mlc_preflight_scan_s *scan)
{
	struct mlc_address_range_s content;
	size_t requested;
	size_t capacity;

	if (node->size < SIZEOF_MM_ALLOCNODE) {
		return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
	}
	capacity = node->size - SIZEOF_MM_ALLOCNODE;
	if (!mm_allocnode_get_requested_size(node, &requested) ||
		requested > capacity) {
		return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
	}
	scan->allocated++;
	if (scan->range == NULL) {
		return MLC_CANDIDATE_SNAPSHOT_OK;
	}
	content.begin = (uintptr_t)node + SIZEOF_MM_ALLOCNODE;
	content.size = requested;
	if (mlc_candidate_range_contains(&content, scan->range)) {
		if (scan->exact && !mlc_candidate_ranges_equal(&content, scan->range)) {
			scan->conflicts++;
		} else {
			scan->matches++;
		}
	} else if (mlc_candidate_ranges_overlap(&content, scan->range)) {
		scan->conflicts++;
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

static int mlc_preflight_scan(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		struct mlc_preflight_scan_s *scan)
{
	size_t heap_index;

	for (heap_index = 0; heap_index < request->heap_count; heap_index++) {
		if (mlc_preflight_budget(workspace, MLC_BUDGET_HEAP_REVALIDATE) < 0) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		const struct mm_heap_s *heap = request->heaps[heap_index];
		size_t region_count;
		size_t region;
		if (heap->mm_holder != getpid() || heap->mm_counts_held != 1) {
			return MLC_CANDIDATE_SNAPSHOT_CONTENTION;
		}
		if (heap_index > 0 && (uintptr_t)request->heaps[heap_index - 1] >=
			(uintptr_t)heap) {
			return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
		}
#if CONFIG_KMM_REGIONS > 1
		region_count = (size_t)heap->mm_nregions;
#else
		region_count = 1;
#endif
		for (region = 0; region < region_count; region++) {
			const struct mm_allocnode_s *node = heap->mm_heapstart[region];
			const struct mm_allocnode_s *end = heap->mm_heapend[region];
			size_t previous;

			if (node->size != SIZEOF_MM_ALLOCNODE ||
				(node->preceding & MM_ALLOC_BIT) == 0) {
				return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
			}
			previous = node->size;

			while (node != end) {
				uintptr_t address = (uintptr_t)node;
				const struct mm_allocnode_s *next;
				if (mlc_preflight_budget(workspace, MLC_BUDGET_HEAP_NODE) < 0 ||
					mlc_preflight_budget(workspace, MLC_BUDGET_LINK_VALIDATE) < 0) {
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
					if ((node->preceding & MM_ALLOC_BIT) != 0) {
						int status = mlc_preflight_node(node, scan);

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
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

static int mlc_preflight_range(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		const struct mlc_address_range_s *range, bool required, bool exact)
{
	struct mlc_preflight_scan_s scan = { range, 0, 0, 0, exact };
	int status = mlc_preflight_scan(request, workspace, &scan);

	if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
		return status;
	}
	return scan.conflicts != 0 || scan.matches > 1 ||
		(required && scan.matches == 0 && scan.conflicts != 0) ?
		MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED : MLC_CANDIDATE_SNAPSHOT_OK;
}

int mlc_candidate_preflight(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace)
{
	struct mlc_preflight_scan_s scan = { NULL, 0, 0, 0, false };
	size_t root_upper = request->mapping_count;
	size_t index;
	int status = mlc_preflight_scan(request, workspace, &scan);

	if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
		return status;
	}
	if (scan.allocated > workspace->candidate_capacity ||
		request->exclusion_count > SIZE_MAX - request->mapping_count ||
		request->exclusion_count + request->mapping_count >
		workspace->exclusion_capacity) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	for (index = 0; index < request->exclusion_count; index++) {
		if (mlc_preflight_budget(workspace, MLC_BUDGET_EXCLUSION) < 0) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		bool required = request->exclusions[index].kind == MLC_EXCLUDE_ACTIVE_TCB ||
			request->exclusions[index].kind == MLC_EXCLUDE_FULL_STACK;

		status = mlc_preflight_range(request, workspace,
			&request->exclusions[index].range,
			required, false);
		if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
			return status;
		}
	}
	for (index = 0; index < request->mapping_count; index++) {
		if (mlc_preflight_budget(workspace, MLC_BUDGET_ROOT_CONTAINER_ENUM) < 0) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		const struct mlc_loadable_mapping_input_s *mapping =
			&request->mappings[index];
		size_t prior;

		if (!mlc_candidate_range_contains(&mapping->declared_container,
			&mapping->mapping)) {
			return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
		status = mlc_preflight_range(request, workspace,
			&mapping->declared_container, true,
			true);
		if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
			return status;
		}
		for (prior = 0; prior < index; prior++) {
			if (mlc_preflight_budget(workspace, MLC_BUDGET_DEDUP) < 0) {
				return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			}
			const struct mlc_address_range_s *other =
				&request->mappings[prior].mapping;

			if (mlc_candidate_ranges_overlap(other, &mapping->mapping) &&
				!mlc_candidate_ranges_equal(other, &mapping->mapping)) {
				return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
			}
		}
		for (prior = 0; prior < request->root_count; prior++) {
			if (mlc_preflight_budget(workspace, MLC_BUDGET_DEDUP) < 0) {
				return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			}
			if (mlc_candidate_ranges_overlap(&mapping->mapping,
				&request->roots[prior].range)) {
				return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
			}
		}
		for (prior = 0; prior < request->exclusion_count; prior++) {
			if (mlc_preflight_budget(workspace, MLC_BUDGET_DEDUP) < 0) {
				return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			}
			if (mlc_candidate_ranges_overlap(&mapping->declared_container,
				&request->exclusions[prior].range)) {
				return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
			}
		}
	}
	for (index = 0; index < request->root_count; index++) {
		if (mlc_preflight_budget(workspace, MLC_BUDGET_ROOT_RANGE) < 0) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		size_t contribution = 1;
		size_t prior;

		if (request->roots[index].kind == MLC_ROOT_BROAD_STATIC) {
			if (request->exclusion_count > SIZE_MAX - contribution ||
				request->heap_count >
				(SIZE_MAX - contribution - request->exclusion_count) /
				CONFIG_KMM_REGIONS) {
				return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			}
			contribution += request->exclusion_count;
			contribution += request->heap_count * CONFIG_KMM_REGIONS;
		}

		if (root_upper > SIZE_MAX - contribution) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		root_upper += contribution;
		for (prior = 0; prior < index; prior++) {
			if (mlc_preflight_budget(workspace, MLC_BUDGET_DEDUP) < 0) {
				return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			}
			const struct mlc_root_input_s *left = &request->roots[prior];
			const struct mlc_root_input_s *right = &request->roots[index];

			if (left->kind == MLC_ROOT_BROAD_STATIC ||
				right->kind == MLC_ROOT_BROAD_STATIC) {
				if (left->kind == right->kind &&
					mlc_candidate_ranges_overlap(&left->range, &right->range)) {
					return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
				}
				continue;
			}
			if (mlc_candidate_ranges_overlap(&left->range, &right->range) &&
				(!mlc_candidate_ranges_equal(&left->range, &right->range) ||
				 left->kind != right->kind)) {
				return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
			}
		}
	}
	return root_upper <= workspace->root_capacity ?
		MLC_CANDIDATE_SNAPSHOT_OK : MLC_CANDIDATE_SNAPSHOT_CAPACITY;
}
