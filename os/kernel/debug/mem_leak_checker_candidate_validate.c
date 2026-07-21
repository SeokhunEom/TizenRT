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

struct mlc_candidate_span_s {
	uintptr_t begin;
	uintptr_t end;
};

static bool mlc_span(const void *pointer, size_t count, size_t element_size,
		struct mlc_candidate_span_s *span)
{
	size_t bytes;
	uintptr_t begin;

	if (count == 0) {
		span->begin = 0;
		span->end = 0;
		return true;
	}
	if (pointer == NULL || element_size == 0 || count > SIZE_MAX / element_size) {
		return false;
	}
	bytes = count * element_size;
	begin = (uintptr_t)pointer;
	if (bytes > UINTPTR_MAX || begin > UINTPTR_MAX - (uintptr_t)bytes) {
		return false;
	}
	span->begin = begin;
	span->end = begin + (uintptr_t)bytes;
	return true;
}

static bool mlc_spans_overlap(const struct mlc_candidate_span_s *left,
		const struct mlc_candidate_span_s *right)
{
	return left->begin != left->end && right->begin != right->end &&
		left->begin < right->end && right->begin < left->end;
}

static size_t mlc_heap_region_count(const struct mm_heap_s *heap)
{
#if CONFIG_KMM_REGIONS > 1
	return (size_t)heap->mm_nregions;
#else
	(void)heap;
	return 1;
#endif
}

static bool mlc_heap_region_span(const struct mm_heap_s *heap, size_t region,
		struct mlc_candidate_span_s *span)
{
	uintptr_t begin = (uintptr_t)heap->mm_heapstart[region];
	uintptr_t end = (uintptr_t)heap->mm_heapend[region];

	if (begin == 0 || end <= begin || end > UINTPTR_MAX - SIZEOF_MM_ALLOCNODE) {
		return false;
	}
	span->begin = begin;
	span->end = end + SIZEOF_MM_ALLOCNODE;
	return true;
}

int mlc_candidate_validate_heap_regions(
		const struct mlc_candidate_snapshot_request_s *request)
{
	size_t heap_index;

	for (heap_index = 0; heap_index < request->heap_count; heap_index++) {
		const struct mm_heap_s *heap = request->heaps[heap_index];
		size_t region_count;
		size_t region;

		if (heap == NULL) {
			return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
		}
		region_count = mlc_heap_region_count(heap);
		if (region_count == 0 || region_count > CONFIG_KMM_REGIONS) {
			return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
		}
		for (region = 0; region < region_count; region++) {
			struct mlc_candidate_span_s current;
			size_t prior_heap;

			if (!mlc_heap_region_span(heap, region, &current)) {
				return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
			}
			for (prior_heap = 0; prior_heap <= heap_index; prior_heap++) {
				const struct mm_heap_s *prior = request->heaps[prior_heap];
				size_t prior_regions = prior_heap == heap_index ? region :
					mlc_heap_region_count(prior);
				size_t prior_region;

				for (prior_region = 0; prior_region < prior_regions;
					prior_region++) {
					struct mlc_candidate_span_s other;

					if (!mlc_heap_region_span(prior, prior_region, &other) ||
						mlc_spans_overlap(&current, &other)) {
						return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
					}
				}
			}
		}
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

int mlc_candidate_validate_request(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		const struct mlc_candidate_snapshot_result_s *result)
{
	struct mlc_candidate_span_s output[3];
	struct mlc_candidate_span_s input[7];
	size_t output_index;

	if (request == NULL || workspace == NULL || result == NULL ||
		request->heaps == NULL || request->heap_count == 0 ||
		workspace->candidates == NULL || workspace->exclusions == NULL ||
		workspace->roots == NULL ||
		(request->exclusion_count > 0 && request->exclusions == NULL) ||
		(request->root_count > 0 && request->roots == NULL) ||
		(request->mapping_count > 0 && request->mappings == NULL) ||
		!mlc_span(workspace->candidates, workspace->candidate_capacity,
			sizeof(*workspace->candidates), &output[0]) ||
		!mlc_span(workspace->exclusions, workspace->exclusion_capacity,
			sizeof(*workspace->exclusions), &output[1]) ||
		!mlc_span(workspace->roots, workspace->root_capacity,
			sizeof(*workspace->roots), &output[2]) ||
		!mlc_span(request, 1, sizeof(*request), &input[0]) ||
		!mlc_span(workspace, 1, sizeof(*workspace), &input[1]) ||
		!mlc_span(result, 1, sizeof(*result), &input[2]) ||
		!mlc_span(request->heaps, request->heap_count,
			sizeof(*request->heaps), &input[3]) ||
		!mlc_span(request->exclusions, request->exclusion_count,
			sizeof(*request->exclusions), &input[4]) ||
		!mlc_span(request->roots, request->root_count,
			sizeof(*request->roots), &input[5]) ||
		!mlc_span(request->mappings, request->mapping_count,
			sizeof(*request->mappings), &input[6])) {
		return MLC_CANDIDATE_SNAPSHOT_INVALID;
	}
	for (output_index = 0; output_index < 3; output_index++) {
		size_t other;

		for (other = output_index + 1; other < 3; other++) {
			if (mlc_spans_overlap(&output[output_index], &output[other])) {
				return MLC_CANDIDATE_SNAPSHOT_INVALID;
			}
		}
		for (other = 0; other < 7; other++) {
			if (mlc_spans_overlap(&output[output_index], &input[other])) {
				return MLC_CANDIDATE_SNAPSHOT_INVALID;
			}
		}
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

int mlc_candidate_validate_storage(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		const struct mlc_candidate_snapshot_result_s *result)
{
	struct mlc_candidate_span_s protected_spans[10];
	size_t protected_count = 0;
	size_t heap_index;

#define MLC_PROTECT(pointer, count, type) \
	do { \
		if (!mlc_span(pointer, count, sizeof(type), \
			&protected_spans[protected_count++])) { \
			return MLC_CANDIDATE_SNAPSHOT_INVALID; \
		} \
	} while (0)

	MLC_PROTECT(request, 1, *request);
	MLC_PROTECT(workspace, 1, *workspace);
	MLC_PROTECT(result, 1, *result);
	MLC_PROTECT(request->heaps, request->heap_count, *request->heaps);
	MLC_PROTECT(request->exclusions, request->exclusion_count,
		*request->exclusions);
	MLC_PROTECT(request->roots, request->root_count, *request->roots);
	MLC_PROTECT(request->mappings, request->mapping_count, *request->mappings);
	MLC_PROTECT(workspace->candidates, workspace->candidate_capacity,
		*workspace->candidates);
	MLC_PROTECT(workspace->exclusions, workspace->exclusion_capacity,
		*workspace->exclusions);
	MLC_PROTECT(workspace->roots, workspace->root_capacity, *workspace->roots);
#undef MLC_PROTECT

	for (heap_index = 0; heap_index < request->heap_count; heap_index++) {
		const struct mm_heap_s *heap = request->heaps[heap_index];
		struct mlc_candidate_span_s heap_object;
		size_t region_count = mlc_heap_region_count(heap);
		size_t region;
		size_t protected_index;

		if (!mlc_span(heap, 1, sizeof(*heap), &heap_object)) {
			return MLC_CANDIDATE_SNAPSHOT_INVALID;
		}
		for (protected_index = 0; protected_index < protected_count;
			protected_index++) {
			if (mlc_spans_overlap(&protected_spans[protected_index],
				&heap_object)) {
				return MLC_CANDIDATE_SNAPSHOT_INVALID;
			}
		}
		for (region = 0; region < region_count; region++) {
			struct mlc_candidate_span_s arena;

			if (!mlc_heap_region_span(heap, region, &arena)) {
				return MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT;
			}
			for (protected_index = 0; protected_index < protected_count;
				protected_index++) {
				if (mlc_spans_overlap(&protected_spans[protected_index],
					&arena)) {
					return MLC_CANDIDATE_SNAPSHOT_INVALID;
				}
			}
		}
	}
	return MLC_CANDIDATE_SNAPSHOT_OK;
}
