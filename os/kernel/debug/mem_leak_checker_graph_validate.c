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

#include "mem_leak_checker_graph.h"
#include "mem_leak_checker_graph_internal.h"

#define MUTABLE_SPAN_COUNT 15

struct byte_span_s {
	uintptr_t begin;
	uintptr_t end;
	bool empty;
};

static int byte_span(const void *pointer, size_t count, size_t element_size,
		struct byte_span_s *span)
{
	size_t bytes;
	uintptr_t begin;

	if (count == 0) {
		*span = (struct byte_span_s){0, 0, true};
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
	*span = (struct byte_span_s){begin, begin + (uintptr_t)bytes, false};
	return MLC_CORE_OK;
}

static bool overlaps(const struct byte_span_s *left,
		const struct byte_span_s *right)
{
	return !left->empty && !right->empty && left->begin < right->end &&
			right->begin < left->end;
}

static int mutable_spans(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		const struct mlc_graph_output_s *output, size_t count,
		struct byte_span_s *spans)
{
	int result;

#define ADD_SPAN(slot, pointer, type) \
	do { \
		result = byte_span(pointer, count, sizeof(type), &spans[slot]); \
		if (result != MLC_CORE_OK) { \
			return result; \
		} \
	} while (0)
	ADD_SPAN(0, workspace->reachability, *workspace->reachability);
	ADD_SPAN(1, workspace->provenance, *workspace->provenance);
	ADD_SPAN(2, workspace->frontier, *workspace->frontier);
	ADD_SPAN(3, workspace->frontier_pending, *workspace->frontier_pending);
	ADD_SPAN(4, workspace->tarjan_index, *workspace->tarjan_index);
	ADD_SPAN(5, workspace->lowlink, *workspace->lowlink);
	ADD_SPAN(6, workspace->on_stack, *workspace->on_stack);
	ADD_SPAN(7, workspace->component_stack, *workspace->component_stack);
	ADD_SPAN(8, workspace->dfs_frames, *workspace->dfs_frames);
	ADD_SPAN(9, workspace->scc_ids, *workspace->scc_ids);
	ADD_SPAN(10, workspace->scc_incoming, *workspace->scc_incoming);
	ADD_SPAN(11, workspace->group_scratch, *workspace->group_scratch);
	ADD_SPAN(12, output->groups, *output->groups);
#undef ADD_SPAN
	result = byte_span(output->group_count, 1, sizeof(*output->group_count),
			&spans[13]);
	if (result == MLC_CORE_OK) {
		result = byte_span(input->index->counters,
				input->index->counters == NULL ? 0 : 1,
				sizeof(*input->index->counters), &spans[14]);
	}
	return result;
}

static int reject_overlap(const struct byte_span_s *read_span,
		const struct byte_span_s *mutable_spans)
{
	size_t current;

	for (current = 0; current < MUTABLE_SPAN_COUNT; current++) {
		if (overlaps(read_span, &mutable_spans[current])) {
			return MLC_CORE_ALIASING_WORKSPACE;
		}
	}
	return MLC_CORE_OK;
}

static int validate_aliases(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		const struct mlc_graph_output_s *output, size_t count)
{
	struct byte_span_s spans[MUTABLE_SPAN_COUNT];
	struct byte_span_s read_span;
	size_t left;
	int result = mutable_spans(input, workspace, output, count, spans);

	if (result != MLC_CORE_OK) {
		return result;
	}
	for (left = 0; left < MUTABLE_SPAN_COUNT; left++) {
		size_t right;

		for (right = left + 1; right < MUTABLE_SPAN_COUNT; right++) {
			if (overlaps(&spans[left], &spans[right])) {
				return MLC_CORE_ALIASING_WORKSPACE;
			}
		}
	}

#define REJECT_READ(pointer, elements, type) \
	do { \
		result = byte_span(pointer, elements, sizeof(type), &read_span); \
		if (result != MLC_CORE_OK) { \
			return result; \
		} \
		result = reject_overlap(&read_span, spans); \
		if (result != MLC_CORE_OK) { \
			return result; \
		} \
	} while (0)
	REJECT_READ(input, 1, *input);
	REJECT_READ(workspace, 1, *workspace);
	REJECT_READ(output, 1, *output);
	REJECT_READ(input->index, 1, *input->index);
	REJECT_READ(input->index->candidates, count, *input->index->candidates);
	REJECT_READ(input->index->sorted_indices, input->index->sorted_capacity,
			*input->index->sorted_indices);
	REJECT_READ(input->index->exact_slots, input->index->exact_capacity,
			*input->index->exact_slots);
	REJECT_READ(input->candidate_sources, count, *input->candidate_sources);
	REJECT_READ(input->roots, input->root_count, *input->roots);
	for (left = 0; left < count; left++) {
		REJECT_READ(input->candidate_sources[left],
				input->index->candidates[left].content_size, uint8_t);
	}
	for (left = 0; left < input->root_count; left++) {
		REJECT_READ(input->roots[left].source, input->roots[left].source_size,
				uint8_t);
	}
#undef REJECT_READ
	return MLC_CORE_OK;
}

int mlc_graph_validate(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		const struct mlc_graph_output_s *output)
{
	size_t current;
	size_t count;
	int result;

	if (input == NULL || workspace == NULL || output == NULL ||
			input->index == NULL || output->group_count == NULL) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	count = input->index->candidate_count;
	if (input->candidate_source_count != count || workspace->capacity < count ||
			output->group_capacity < count) {
		return workspace->capacity < count ? MLC_CORE_INSUFFICIENT_WORKSPACE :
				MLC_CORE_INSUFFICIENT_OUTPUT;
	}
	if (input->root_count > 0 && input->roots == NULL) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	if (count > 0 && input->candidate_sources == NULL) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	for (current = 0; current < count; current++) {
		if (input->index->candidates[current].content_size > 0 &&
				input->candidate_sources[current] == NULL) {
			return MLC_CORE_INVALID_ARGUMENT;
		}
	}
	for (current = 0; current < input->root_count; current++) {
		if (input->roots[current].source_size > 0 &&
				input->roots[current].source == NULL) {
			return MLC_CORE_INVALID_ARGUMENT;
		}
		if (input->roots[current].source_size > UINTPTR_MAX ||
				input->roots[current].source_begin > UINTPTR_MAX -
				(uintptr_t)input->roots[current].source_size) {
			return MLC_CORE_INVALID_RANGE;
		}
	}
	result = validate_aliases(input, workspace, output, count);
	return result == MLC_CORE_OK ? mlc_candidate_index_validate(input->index) :
			result;
}
