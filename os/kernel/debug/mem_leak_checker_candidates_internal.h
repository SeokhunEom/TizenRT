#ifndef __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_CANDIDATES_INTERNAL_H
#define __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_CANDIDATES_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mem_leak_checker_candidates.h"

static inline bool mlc_candidate_range_end(
		const struct mlc_address_range_s *range, uintptr_t *end)
{
	if (range->size == 0 || range->size > UINTPTR_MAX ||
		range->begin > UINTPTR_MAX - (uintptr_t)range->size) {
		return false;
	}
	*end = range->begin + (uintptr_t)range->size;
	return true;
}

static inline bool mlc_candidate_range_contains(
		const struct mlc_address_range_s *outer,
		const struct mlc_address_range_s *inner)
{
	uintptr_t outer_end;
	uintptr_t inner_end;

	return mlc_candidate_range_end(outer, &outer_end) &&
		mlc_candidate_range_end(inner, &inner_end) &&
		inner->begin >= outer->begin && inner_end <= outer_end;
}

static inline bool mlc_candidate_ranges_overlap(
		const struct mlc_address_range_s *left,
		const struct mlc_address_range_s *right)
{
	uintptr_t left_end;
	uintptr_t right_end;

	return mlc_candidate_range_end(left, &left_end) &&
		mlc_candidate_range_end(right, &right_end) &&
		left->begin < right_end && right->begin < left_end;
}

static inline bool mlc_candidate_ranges_equal(
		const struct mlc_address_range_s *left,
		const struct mlc_address_range_s *right)
{
	return left->begin == right->begin && left->size == right->size;
}

size_t mlc_candidate_find_containing_allocation(
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t allocated, const struct mlc_address_range_s *range);
int mlc_candidate_mark_exclusion(
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t allocated, size_t allocation_index,
		enum mlc_exclusion_kind_e kind, size_t *excluded);
int mlc_candidate_collect_mappings(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t allocated, size_t *excluded, size_t *root_count);
int mlc_candidate_collect_roots(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		size_t *root_count);
int mlc_candidate_validate_request(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		const struct mlc_candidate_snapshot_result_s *result);
int mlc_candidate_validate_heap_regions(
		const struct mlc_candidate_snapshot_request_s *request);
int mlc_candidate_validate_storage(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		const struct mlc_candidate_snapshot_result_s *result);
int mlc_candidate_validate_provenance(
		const struct mlc_candidate_snapshot_request_s *request);
int mlc_candidate_preflight(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace);

#endif
