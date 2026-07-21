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

static int mlc_tarjan_budget_take(enum mlc_budget_counter_e counter)
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

static void push_frame(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace, size_t node,
		size_t *next_index, size_t *component_count, size_t *frame_count)
{
	workspace->tarjan_index[node] = *next_index;
	workspace->lowlink[node] = *next_index;
	(*next_index)++;
	workspace->component_stack[(*component_count)++] = node;
	workspace->on_stack[node] = true;
	workspace->dfs_frames[*frame_count] =
		(struct mlc_tarjan_frame_s){node, 0};
	(*frame_count)++;
	(void)input;
}

static void close_component(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace, size_t root,
		size_t *component_count, struct mlc_leak_group_s *groups,
		size_t *group_count)
{
	struct mlc_leak_group_s group = {
		input->index->candidates[root].payload_begin, 0, true, 0
	};
	size_t member;

	do {
		member = workspace->component_stack[--(*component_count)];
		workspace->on_stack[member] = false;
		workspace->scc_ids[member] = *group_count;
		group.member_count++;
		if (input->index->candidates[member].payload_begin < group.representative) {
			group.representative = input->index->candidates[member].payload_begin;
		}
	} while (member != root);
	groups[(*group_count)++] = group;
}

static int visit_from(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace, size_t start,
		size_t *next_index, size_t *component_count,
		struct mlc_leak_group_s *groups, size_t *group_count)
{
	size_t frame_count = 0;

	push_frame(input, workspace, start, next_index, component_count, &frame_count);
	while (frame_count > 0) {
		if (mlc_tarjan_budget_take(MLC_BUDGET_TARJAN_FRAME) < 0) {
			return MLC_CORE_INSUFFICIENT_WORKSPACE;
		}
		struct mlc_tarjan_frame_s *frame = &workspace->dfs_frames[frame_count - 1];
		struct mlc_match_s match;
		bool found;
		int result = mlc_graph_next_edge(input, frame->node,
				&frame->next_offset, &found, &match);

		if (result != MLC_CORE_OK) {
			return result;
		}
		if (found && workspace->reachability[match.candidate_index] == MLC_REACH_NONE) {
			if (mlc_tarjan_budget_take(MLC_BUDGET_EDGE_RESCAN) < 0) {
				return MLC_CORE_INSUFFICIENT_WORKSPACE;
			}
			size_t target = match.candidate_index;

			if (workspace->tarjan_index[target] == MLC_EMPTY_SLOT) {
				push_frame(input, workspace, target, next_index,
						component_count, &frame_count);
				continue;
			}
			if (workspace->on_stack[target] &&
					workspace->tarjan_index[target] < workspace->lowlink[frame->node]) {
				workspace->lowlink[frame->node] = workspace->tarjan_index[target];
			}
			continue;
		}
		if (found) {
			continue;
		}
		if (workspace->lowlink[frame->node] == workspace->tarjan_index[frame->node]) {
			close_component(input, workspace, frame->node, component_count,
					groups, group_count);
		}
		frame_count--;
		if (frame_count > 0) {
			size_t parent = workspace->dfs_frames[frame_count - 1].node;

			if (workspace->lowlink[frame->node] < workspace->lowlink[parent]) {
				workspace->lowlink[parent] = workspace->lowlink[frame->node];
			}
		}
	}
	return MLC_CORE_OK;
}

static int classify_groups(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		struct mlc_leak_group_s *groups, size_t group_count)
{
	size_t source;

	for (source = 0; source < input->index->candidate_count; source++) {
		if (mlc_tarjan_budget_take(MLC_BUDGET_EDGE_RESCAN) < 0) {
			return MLC_CORE_INSUFFICIENT_WORKSPACE;
		}
		size_t offset = 0;
		bool found;

		if (workspace->reachability[source] != MLC_REACH_NONE) {
			continue;
		}
		do {
			struct mlc_match_s match;
			int result = mlc_graph_next_edge(input, source, &offset, &found, &match);

			if (result != MLC_CORE_OK) {
				return result;
			}
			if (found && workspace->reachability[match.candidate_index] == MLC_REACH_NONE) {
				size_t source_scc = workspace->scc_ids[source];
				size_t target_scc = workspace->scc_ids[match.candidate_index];

				groups[target_scc].strongest_provenance |=
						mlc_graph_edge_provenance(&match);
				if (source_scc != target_scc) {
					workspace->scc_incoming[target_scc] = true;
				}
			}
		} while (found);
	}
	for (source = 0; source < group_count; source++) {
		groups[source].direct = !workspace->scc_incoming[source];
	}
	return MLC_CORE_OK;
}

static int sort_groups(struct mlc_leak_group_s *groups, size_t group_count)
{
	size_t current;

	for (current = 1; current < group_count; current++) {
		struct mlc_leak_group_s group = groups[current];
		size_t insertion = current;

		while (insertion > 0 &&
				groups[insertion - 1].representative > group.representative) {
			if (mlc_tarjan_budget_take(MLC_BUDGET_INDEX_SORT_COMPARE) < 0) {
				return MLC_CORE_INSUFFICIENT_WORKSPACE;
			}
			groups[insertion] = groups[insertion - 1];
			if (mlc_tarjan_budget_take(MLC_BUDGET_INDEX_SORT_MOVE) < 0) {
				return MLC_CORE_INSUFFICIENT_WORKSPACE;
			}
			insertion--;
		}
		groups[insertion] = group;
	}
	return MLC_CORE_OK;
}

int mlc_graph_tarjan(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		struct mlc_leak_group_s *groups, size_t *group_count)
{
	size_t count = input->index->candidate_count;
	size_t next_index = 0;
	size_t component_count = 0;
	size_t current;
	int result;

	for (current = 0; current < count; current++) {
		workspace->tarjan_index[current] = MLC_EMPTY_SLOT;
		workspace->lowlink[current] = MLC_EMPTY_SLOT;
		workspace->scc_ids[current] = MLC_EMPTY_SLOT;
		workspace->on_stack[current] = false;
		workspace->scc_incoming[current] = false;
	}
	*group_count = 0;
	for (current = 0; current < count; current++) {
		if (workspace->reachability[current] == MLC_REACH_NONE &&
				workspace->tarjan_index[current] == MLC_EMPTY_SLOT) {
			result = visit_from(input, workspace, current, &next_index,
					&component_count, groups, group_count);
			if (result != MLC_CORE_OK) {
				return result;
			}
		}
	}
	result = classify_groups(input, workspace, groups, *group_count);
	if (result == MLC_CORE_OK) {
		result = sort_groups(groups, *group_count);
	}
	return result;
}
