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

#include "mem_leak_checker_graph.h"
#include "mem_leak_checker_core_internal.h"
#include "mem_leak_checker_graph_internal.h"

static int mlc_graph_budget_take(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e counter)
{
	int ret;

	if (budget == NULL) {
		return 0;
	}
	ret = mlc_budget_chunk_begin(budget, counter, 1,
		mlc_budget_clock_now());
	return ret < 0 ? ret : mlc_budget_chunk_end(budget,
		mlc_budget_clock_now());
}

struct frontier_state_s {
	size_t head;
	size_t tail;
	size_t count;
};

uint8_t mlc_graph_edge_provenance(const struct mlc_match_s *match)
{
	uint8_t provenance = 0;

	if (match->target_kind == MLC_TARGET_EXACT &&
			match->alignment == MLC_SOURCE_ALIGNED) {
		return MLC_PROVENANCE_ALIGNED_EXACT;
	}
	if (match->target_kind == MLC_TARGET_INTERIOR) {
		provenance |= MLC_PROVENANCE_INTERIOR;
	}
	if (match->alignment == MLC_SOURCE_UNALIGNED) {
		provenance |= MLC_PROVENANCE_UNALIGNED;
	}
	return provenance;
}

int mlc_graph_next_edge(const struct mlc_graph_input_s *input,
		size_t source_index, size_t *offset, bool *found,
		struct mlc_match_s *match)
{
	const struct mlc_candidate_s *candidate;

	if (source_index >= input->index->candidate_count || offset == NULL ||
			found == NULL || match == NULL) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	candidate = &input->index->candidates[source_index];
	while (*offset <= candidate->content_size &&
			candidate->content_size - *offset >= sizeof(uintptr_t)) {
		if (mlc_graph_budget_take(mlc_budget_current(),
			MLC_BUDGET_POINTER_WINDOW) < 0) {
			return MLC_CORE_INSUFFICIENT_WORKSPACE;
		}
		size_t current = *offset;
		struct mlc_lookup_s lookup;
		uintptr_t value;

		(*offset)++;
		memcpy(&value,
				(const uint8_t *)input->candidate_sources[source_index] + current,
				sizeof(value));
		if (mlc_candidate_lookup_validated(input->index, value, found, &lookup) !=
				MLC_CORE_OK) {
			return MLC_CORE_CORRUPT_INDEX;
		}
		if (*found) {
			*match = (struct mlc_match_s){
				current, value, lookup.candidate_id, lookup.candidate_index,
				lookup.kind, (candidate->payload_begin + current) % sizeof(uintptr_t) == 0 ?
						MLC_SOURCE_ALIGNED : MLC_SOURCE_UNALIGNED
			};
			*found = true;
			return MLC_CORE_OK;
		}
	}
	*found = false;
	return MLC_CORE_OK;
}

static int update_reachability(const struct mlc_match_s *match,
		enum mlc_reachability_e source_reach,
		const struct mlc_graph_workspace_s *workspace,
		struct frontier_state_s *frontier)
{
	enum mlc_reachability_e reach =
		mlc_graph_edge_provenance(match) == MLC_PROVENANCE_ALIGNED_EXACT &&
		source_reach == MLC_REACH_STRONG ? MLC_REACH_STRONG : MLC_REACH_AMBIGUOUS;
	size_t target = match->candidate_index;
	if (mlc_graph_budget_take(mlc_budget_current(), MLC_BUDGET_FRONTIER_POP) < 0) {
		return MLC_CORE_INSUFFICIENT_WORKSPACE;
	}

	workspace->provenance[target] |= mlc_graph_edge_provenance(match);
	if (reach <= workspace->reachability[target]) {
		return MLC_CORE_OK;
	}
	workspace->reachability[target] = reach;
	if (!workspace->frontier_pending[target]) {
		if (frontier->count >= workspace->capacity) {
			return MLC_CORE_INSUFFICIENT_WORKSPACE;
		}
		workspace->frontier[frontier->tail] = target;
		frontier->tail = (frontier->tail + 1) % workspace->capacity;
		frontier->count++;
		workspace->frontier_pending[target] = true;
	}
	return MLC_CORE_OK;
}

static int trace_roots(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		struct frontier_state_s *frontier)
{
	size_t root;

	for (root = 0; root < input->root_count; root++) {
		if (mlc_graph_budget_take(mlc_budget_current(), MLC_BUDGET_ROOT_RANGE) < 0) {
			return MLC_CORE_INSUFFICIENT_WORKSPACE;
		}
		size_t offset = 0;
		const struct mlc_graph_root_range_s *range = &input->roots[root];

		while (offset <= range->source_size &&
				range->source_size - offset >= sizeof(uintptr_t)) {
			struct mlc_match_s match;
			struct mlc_lookup_s lookup;
			uintptr_t value;
			bool found;
			int result;

			memcpy(&value, (const uint8_t *)range->source + offset,
					sizeof(value));
			result = mlc_candidate_lookup_validated(input->index, value, &found,
					&lookup);
			if (result != MLC_CORE_OK) {
				return result;
			}
			offset++;
			if (found) {
				match = (struct mlc_match_s){
					offset - 1, value, lookup.candidate_id, lookup.candidate_index,
					lookup.kind, (range->source_begin + offset - 1) % sizeof(uintptr_t) == 0 ?
							MLC_SOURCE_ALIGNED : MLC_SOURCE_UNALIGNED
				};
				result = update_reachability(&match, MLC_REACH_STRONG,
						workspace, frontier);
				if (result != MLC_CORE_OK) {
					return result;
				}
			}
		}
	}
	return MLC_CORE_OK;
}

static int trace_frontier(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		struct frontier_state_s *frontier)
{
	while (frontier->count > 0) {
		if (mlc_graph_budget_take(mlc_budget_current(), MLC_BUDGET_FRONTIER_POP) < 0) {
			return MLC_CORE_INSUFFICIENT_WORKSPACE;
		}
		size_t source = workspace->frontier[frontier->head];
		size_t offset = 0;
		bool found = false;

		frontier->head = (frontier->head + 1) % workspace->capacity;
		frontier->count--;
		workspace->frontier_pending[source] = false;
		do {
			struct mlc_match_s match;
			int result = mlc_graph_next_edge(input, source, &offset, &found, &match);

			if (result != MLC_CORE_OK) {
				return result;
			}
			if (found) {
				result = update_reachability(&match,
						workspace->reachability[source], workspace, frontier);
				if (result != MLC_CORE_OK) {
					return result;
				}
			}
		} while (found);
	}
	return MLC_CORE_OK;
}

int mlc_graph_analyze(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		const struct mlc_graph_output_s *output)
{
	size_t count;
	size_t group_count = 0;
	struct frontier_state_s frontier = {0, 0, 0};
	int result = mlc_graph_validate(input, workspace, output);

	if (result != MLC_CORE_OK) {
		return result;
	}
	count = input->index->candidate_count;
	memset(workspace->reachability, 0, count * sizeof(*workspace->reachability));
	memset(workspace->provenance, 0, count * sizeof(*workspace->provenance));
	memset(workspace->frontier_pending, 0,
			count * sizeof(*workspace->frontier_pending));
	result = trace_roots(input, workspace, &frontier);
	if (result == MLC_CORE_OK) {
		result = trace_frontier(input, workspace, &frontier);
	}
	if (result == MLC_CORE_OK) {
		result = mlc_graph_tarjan(input, workspace, workspace->group_scratch,
				&group_count);
	}
	if (result == MLC_CORE_OK) {
		memcpy(output->groups, workspace->group_scratch,
				group_count * sizeof(*output->groups));
		*output->group_count = group_count;
	}
	return result;
}
