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

#include "mem_leak_checker_candidates_internal.h"

static int mlc_root_budget(
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

static int mlc_append_root(
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		const struct mlc_address_range_s *range, enum mlc_root_kind_e kind,
		size_t *root_count)
{
	size_t index;
	uintptr_t end;

	if (!mlc_candidate_range_end(range, &end) || (int)kind < 0 ||
		kind > MLC_ROOT_LOADABLE_WRITABLE) {
		return MLC_CANDIDATE_SNAPSHOT_INVALID;
	}
	for (index = 0; index < *root_count; index++) {
		if (mlc_candidate_ranges_equal(&workspace->roots[index].range, range) &&
			workspace->roots[index].kind == kind) {
			return MLC_CANDIDATE_SNAPSHOT_OK;
		}
		if (mlc_candidate_ranges_overlap(&workspace->roots[index].range, range)) {
			return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
	}
	if (*root_count >= workspace->root_capacity) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	workspace->roots[*root_count].range = *range;
	workspace->roots[*root_count].kind = kind;
	(*root_count)++;
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

int mlc_candidate_collect_mappings(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t allocated, size_t *excluded, size_t *root_count)
{
	size_t index;

	for (index = 0; index < request->mapping_count; index++) {
		if (mlc_root_budget(workspace, MLC_BUDGET_ROOT_CONTAINER_ENUM) < 0) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		const struct mlc_loadable_mapping_input_s *mapping =
			&request->mappings[index];
		size_t containing;
		struct mlc_address_range_s content;
		size_t prior;
		int status;

		if (!mlc_candidate_range_contains(&mapping->declared_container,
			&mapping->mapping)) {
			return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
		containing = mlc_candidate_find_containing_allocation(workspace,
			allocated, &mapping->declared_container);
		if (containing >= SIZE_MAX - 1) {
			return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
		content.begin = workspace->candidates[containing].payload_begin;
		content.size = workspace->candidates[containing].content_size;
		if (!mlc_candidate_ranges_equal(&content,
			&mapping->declared_container)) {
			return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
		for (prior = 0; prior < index; prior++) {
			const struct mlc_loadable_mapping_input_s *other =
				&request->mappings[prior];

			if (mlc_candidate_ranges_overlap(&other->mapping,
				&mapping->mapping) && !mlc_candidate_ranges_equal(
				&other->mapping, &mapping->mapping)) {
				return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
			}
		}
		status = mlc_candidate_mark_exclusion(workspace, allocated, containing,
			MLC_EXCLUDE_LOADABLE_ROOT_CONTAINER, excluded);
		if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
			return status;
		}
		status = mlc_append_root(workspace, &mapping->mapping,
			MLC_ROOT_LOADABLE_WRITABLE, root_count);
		if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
			return status;
		}
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

static bool mlc_heap_arena_cut(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_address_range_s *broad, uintptr_t cursor,
		struct mlc_address_range_s *cut)
{
	bool found = false;
	size_t heap_index;

	for (heap_index = 0; heap_index < request->heap_count; heap_index++) {
		const struct mm_heap_s *heap = request->heaps[heap_index];
		size_t region_count;
		size_t region;

#if CONFIG_KMM_REGIONS > 1
		region_count = (size_t)heap->mm_nregions;
#else
		region_count = 1;
#endif
		for (region = 0; region < region_count; region++) {
			struct mlc_address_range_s arena;
			uintptr_t arena_end = (uintptr_t)heap->mm_heapend[region] +
				SIZEOF_MM_ALLOCNODE;

			arena.begin = (uintptr_t)heap->mm_heapstart[region];
			arena.size = arena_end - arena.begin;
			if (mlc_candidate_ranges_overlap(&arena, broad) &&
				arena_end > cursor && (!found || arena.begin < cut->begin)) {
				*cut = arena;
				found = true;
			}
		}
	}
	return found;
}

static bool mlc_next_source_cut(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_address_range_s *broad, uintptr_t cursor,
		struct mlc_address_range_s *cut)
{
	bool found = mlc_heap_arena_cut(request, broad, cursor, cut);
	size_t index;

	for (index = 0; index < request->exclusion_count; index++) {
		const struct mlc_address_range_s *range =
			&request->exclusions[index].range;
		uintptr_t end;

		if (mlc_candidate_range_end(range, &end) &&
			mlc_candidate_ranges_overlap(range, broad) && end > cursor &&
			(!found || range->begin < cut->begin)) {
			*cut = *range;
			found = true;
		}
	}
	return found;
}

static int mlc_subtract_broad_root(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		const struct mlc_address_range_s *broad, size_t *root_count)
{
	uintptr_t broad_end;
	uintptr_t cursor;

	if (!mlc_candidate_range_end(broad, &broad_end)) {
		return MLC_CANDIDATE_SNAPSHOT_INVALID;
	}
	cursor = broad->begin;
	while (cursor < broad_end) {
		struct mlc_address_range_s cut;
		uintptr_t cut_end;
		int status;

		if (!mlc_next_source_cut(request, broad, cursor, &cut)) {
			struct mlc_address_range_s tail = { cursor, broad_end - cursor };

			return mlc_append_root(workspace, &tail, MLC_ROOT_BROAD_STATIC,
				root_count);
		}
		if (!mlc_candidate_range_end(&cut, &cut_end)) {
			return MLC_CANDIDATE_SNAPSHOT_INVALID;
		}
		if (cut.begin > cursor) {
			struct mlc_address_range_s prefix = { cursor, cut.begin - cursor };

			status = mlc_append_root(workspace, &prefix,
				MLC_ROOT_BROAD_STATIC, root_count);
			if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
				return status;
			}
		}
		if (cut_end <= cursor) {
			return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
		cursor = cut_end < broad_end ? cut_end : broad_end;
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

int mlc_candidate_collect_roots(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t *root_count)
{
	size_t index;

	for (index = 0; index < request->root_count; index++) {
		if (mlc_root_budget(workspace, MLC_BUDGET_ROOT_RANGE) < 0) {
			return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
		const struct mlc_root_input_s *root = &request->roots[index];
		int status;

		if (root->kind == MLC_ROOT_LOADABLE_WRITABLE) {
			return MLC_CANDIDATE_SNAPSHOT_INVALID;
		}
		if (root->kind != MLC_ROOT_BROAD_STATIC &&
			root->kind != MLC_ROOT_TASK_REGISTERS) {
			enum mlc_exclusion_kind_e required = root->kind ==
				MLC_ROOT_ACTIVE_TCB ? MLC_EXCLUDE_ACTIVE_TCB :
				MLC_EXCLUDE_FULL_STACK;
			size_t exclusion;
			bool contained = false;

			for (exclusion = 0; exclusion < request->exclusion_count;
				exclusion++) {
				if (request->exclusions[exclusion].kind == required &&
					mlc_candidate_range_contains(
					&request->exclusions[exclusion].range, &root->range)) {
					contained = true;
					break;
				}
			}
			if (!contained) {
				return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
			}
		}
		if (root->kind == MLC_ROOT_BROAD_STATIC) {
			status = mlc_subtract_broad_root(request, workspace,
				&root->range, root_count);
		} else {
			status = mlc_append_root(workspace, &root->range, root->kind,
				root_count);
		}
		if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
			return status;
		}
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

int mlc_candidate_validate_provenance(
		const struct mlc_candidate_snapshot_request_s *request)
{
	size_t exclusion_index;
	size_t root_index;

	for (root_index = 0; root_index < request->root_count; root_index++) {
		const struct mlc_root_input_s *root = &request->roots[root_index];
		uintptr_t end;
		size_t matches = 0;

		if ((int)root->kind < 0 || root->kind > MLC_ROOT_LOADABLE_WRITABLE ||
			!mlc_candidate_range_end(&root->range, &end) ||
			root->kind == MLC_ROOT_LOADABLE_WRITABLE) {
			return MLC_CANDIDATE_SNAPSHOT_INVALID;
		}
		if (root->kind == MLC_ROOT_BROAD_STATIC ||
			root->kind == MLC_ROOT_TASK_REGISTERS) {
			continue;
		}
		for (exclusion_index = 0;
			exclusion_index < request->exclusion_count; exclusion_index++) {
			const struct mlc_exclusion_range_s *exclusion =
				&request->exclusions[exclusion_index];
			bool matches_kind = root->kind == MLC_ROOT_ACTIVE_TCB ?
				exclusion->kind == MLC_EXCLUDE_ACTIVE_TCB :
				exclusion->kind == MLC_EXCLUDE_FULL_STACK;
			bool matches_range = root->kind == MLC_ROOT_ACTIVE_TCB ?
				mlc_candidate_ranges_equal(&exclusion->range, &root->range) :
				mlc_candidate_range_contains(&exclusion->range, &root->range);

			if (matches_kind && matches_range) {
				matches++;
			}
		}
		if (matches != 1) {
			return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
	}

	for (exclusion_index = 0; exclusion_index < request->exclusion_count;
		exclusion_index++) {
		const struct mlc_exclusion_range_s *exclusion =
			&request->exclusions[exclusion_index];
		uintptr_t end;
		size_t matches = 0;

		if ((int)exclusion->kind < 0 ||
			exclusion->kind >= MLC_EXCLUDE_KIND_COUNT ||
			!mlc_candidate_range_end(&exclusion->range, &end)) {
			return MLC_CANDIDATE_SNAPSHOT_INVALID;
		}
		if (exclusion->kind != MLC_EXCLUDE_ACTIVE_TCB &&
			exclusion->kind != MLC_EXCLUDE_FULL_STACK) {
			continue;
		}
		for (root_index = 0; root_index < request->root_count; root_index++) {
			const struct mlc_root_input_s *root = &request->roots[root_index];
			bool matches_kind = exclusion->kind == MLC_EXCLUDE_ACTIVE_TCB ?
				root->kind == MLC_ROOT_ACTIVE_TCB :
				(root->kind == MLC_ROOT_TASK_STACK_LIVE ||
				 root->kind == MLC_ROOT_IRQ_STACK_LIVE);
			bool matches_range = exclusion->kind == MLC_EXCLUDE_ACTIVE_TCB ?
				mlc_candidate_ranges_equal(&exclusion->range, &root->range) :
				mlc_candidate_range_contains(&exclusion->range, &root->range);

			if (matches_kind && matches_range) {
				matches++;
			}
		}
		if (matches != 1) {
			return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}
