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
#include <string.h>

#include "mem_leak_checker_core.h"
#include "mem_leak_checker_core_internal.h"

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

struct byte_span_s {
	uintptr_t begin;
	uintptr_t end;
	bool empty;
};

static bool add_overflows(uintptr_t begin, size_t size)
{
	return size > UINTPTR_MAX || begin > UINTPTR_MAX - (uintptr_t)size;
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

static int validate_output_spans(const struct mlc_candidate_index_s *index,
		const struct mlc_candidate_index_s *index_object,
		struct mlc_match_s *matches, size_t match_capacity, size_t *match_count)
{
	struct byte_span_s spans[8];
	size_t output;
	int result;

	result = byte_span(matches, match_capacity, sizeof(*matches), &spans[0]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(match_count, 1, sizeof(*match_count), &spans[1]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(index_object, 1, sizeof(*index_object), &spans[2]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(index->workspace, 1, sizeof(*index->workspace), &spans[3]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(index->candidates, index->candidate_count,
			sizeof(*index->candidates), &spans[4]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(index->sorted_indices, index->sorted_capacity,
			sizeof(*index->sorted_indices), &spans[5]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(index->exact_slots, index->exact_capacity,
			sizeof(*index->exact_slots), &spans[6]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(index->counters, index->counters == NULL ? 0 : 1,
			sizeof(*index->counters), &spans[7]);
	if (result != MLC_CORE_OK) {
		return result;
	}
	for (output = 0; output < 2; output++) {
		size_t other;

		for (other = output + 1; other < COUNT_OF(spans); other++) {
			if (spans_overlap(&spans[output], &spans[other])) {
				return MLC_CORE_ALIASING_WORKSPACE;
			}
		}
	}
	return MLC_CORE_OK;
}

static int validate_source_output_spans(const void *source, size_t source_size,
		struct mlc_match_s *matches, size_t match_capacity, size_t *match_count)
{
	struct byte_span_s source_span;
	struct byte_span_s output_span;
	struct byte_span_s count_span;
	int result;

	result = byte_span(source, source_size, 1, &source_span);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(matches, match_capacity, sizeof(*matches), &output_span);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = byte_span(match_count, 1, sizeof(*match_count), &count_span);
	if (result != MLC_CORE_OK) {
		return result;
	}
	return spans_overlap(&source_span, &output_span) ||
			spans_overlap(&source_span, &count_span) ?
			MLC_CORE_ALIASING_WORKSPACE : MLC_CORE_OK;
}

static int scan_pass(struct mlc_candidate_index_s *index, const uint8_t *bytes,
		size_t source_size, uintptr_t source_begin, struct mlc_match_s *matches,
		bool emit, size_t *match_count)
{
	size_t offset;
	int result;

	if (source_size < sizeof(uintptr_t)) {
		return MLC_CORE_OK;
	}
	for (offset = 0; offset <= source_size - sizeof(uintptr_t); offset++) {
		struct mlc_lookup_s lookup;
		uintptr_t value;
		bool found;

		if (!emit && index->counters != NULL) {
			index->counters->scanned_windows++;
		}
		memcpy(&value, bytes + offset, sizeof(value));
		result = mlc_candidate_lookup_validated(index, value, &found, &lookup);
		if (result != MLC_CORE_OK) {
			return result;
		}
		if (found) {
			if (*match_count == SIZE_MAX) {
				return MLC_CORE_INVALID_RANGE;
			}
			if (emit) {
				struct mlc_match_s *match = &matches[*match_count];

				match->source_offset = offset;
				match->value = value;
				match->candidate_id = lookup.candidate_id;
				match->candidate_index = lookup.candidate_index;
				match->target_kind = lookup.kind;
				match->alignment = (source_begin + offset) % sizeof(uintptr_t) == 0 ?
						MLC_SOURCE_ALIGNED : MLC_SOURCE_UNALIGNED;
			}
			(*match_count)++;
		}
	}
	return MLC_CORE_OK;
}

static int scan_validated(struct mlc_candidate_index_s *index, const void *source,
		size_t source_size, uintptr_t source_begin, struct mlc_match_s *matches,
		size_t match_capacity, size_t *match_count)
{
	size_t required = 0;
	size_t emitted = 0;
	int result;

	if (add_overflows(source_begin, source_size)) {
		return MLC_CORE_INVALID_RANGE;
	}
	result = validate_source_output_spans(source, source_size, matches,
			match_capacity, match_count);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = scan_pass(index, source, source_size, source_begin, NULL, false,
			&required);
	if (result != MLC_CORE_OK) {
		return result;
	}
	if (required > match_capacity) {
		return MLC_CORE_INSUFFICIENT_OUTPUT;
	}
	result = scan_pass(index, source, source_size, source_begin, matches, true,
			&emitted);
	if (result != MLC_CORE_OK || emitted != required) {
		return result != MLC_CORE_OK ? result : MLC_CORE_CORRUPT_INDEX;
	}
	*match_count = emitted;
	return MLC_CORE_OK;
}

int mlc_scan_range(const struct mlc_candidate_index_s *index,
		const void *source, size_t source_size, uintptr_t source_begin,
		struct mlc_match_s *matches, size_t match_capacity, size_t *match_count)
{
	struct mlc_candidate_index_s snapshot;
	int result;

	if (index == NULL || match_count == NULL ||
			(source_size > 0 && source == NULL) ||
			(match_capacity > 0 && matches == NULL)) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	snapshot = *index;
	result = validate_output_spans(&snapshot, index, matches, match_capacity,
			match_count);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = mlc_candidate_index_validate(&snapshot);
	if (result != MLC_CORE_OK) {
		return result;
	}
	return scan_validated(&snapshot, source, source_size, source_begin, matches,
			match_capacity, match_count);
}

int mlc_scan_candidate(const struct mlc_candidate_index_s *index,
		size_t candidate_index, const void *source, struct mlc_match_s *matches,
		size_t match_capacity, size_t *match_count)
{
	struct mlc_candidate_index_s snapshot;
	const struct mlc_candidate_s *candidate;
	int result;

	if (index == NULL || match_count == NULL ||
			(match_capacity > 0 && matches == NULL)) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	snapshot = *index;
	result = validate_output_spans(&snapshot, index, matches, match_capacity,
			match_count);
	if (result != MLC_CORE_OK) {
		return result;
	}
	result = mlc_candidate_index_validate(&snapshot);
	if (result != MLC_CORE_OK || candidate_index >= snapshot.candidate_count) {
		return result != MLC_CORE_OK ? result : MLC_CORE_INVALID_ARGUMENT;
	}
	candidate = &snapshot.candidates[candidate_index];
	if (candidate->state != MLC_CANDIDATE_ALLOCATED || !candidate->extent_valid) {
		return MLC_CORE_NOT_SCANNABLE;
	}
	if (candidate->content_size > 0 && source == NULL) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	return scan_validated(&snapshot, source, candidate->content_size,
			candidate->payload_begin, matches, match_capacity, match_count);
}
