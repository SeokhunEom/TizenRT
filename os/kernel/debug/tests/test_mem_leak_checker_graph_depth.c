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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "mem_leak_checker_core.h"
#include "mem_leak_checker_graph.h"

#define BASE(index) ((uintptr_t)(0x1000 + (index) * 0x100))
#define MLC_TEST_MAX_ALLOC_COUNT 3000

void mlc_test_configured_max_depth(void)
{
	const size_t count = MLC_TEST_MAX_ALLOC_COUNT;
	struct mlc_candidate_s *candidates = calloc(count, sizeof(*candidates));
	uintptr_t *payloads = calloc(count, sizeof(*payloads));
	const void **sources = calloc(count, sizeof(*sources));
	size_t *order = calloc(count, sizeof(*order));
	size_t *exact = calloc(count * 2 + 1, sizeof(*exact));
	struct mlc_candidate_index_s index;
	struct mlc_graph_workspace_s graph_workspace;
	struct mlc_graph_input_s input;
	struct mlc_graph_output_s output;
	struct mlc_leak_group_s *groups = calloc(count, sizeof(*groups));
	size_t group_count = 0;
	size_t current;

	assert(candidates != NULL && payloads != NULL && sources != NULL &&
			order != NULL && exact != NULL && groups != NULL);
	for (current = 0; current < count; current++) {
		candidates[current] = (struct mlc_candidate_s){
			BASE(current), sizeof(payloads[current]), sizeof(payloads[current]),
			current, MLC_CANDIDATE_ALLOCATED, true
		};
		sources[current] = &payloads[current];
		if (current + 1 < count) {
			payloads[current] = BASE(current + 1);
		}
	}
	assert(mlc_candidate_index_build(&index, candidates, count,
			&(struct mlc_candidate_workspace_s){
				order, count, exact, count * 2 + 1, NULL
			}) == MLC_CORE_OK);
	graph_workspace = (struct mlc_graph_workspace_s){
		calloc(count, sizeof(*graph_workspace.reachability)),
		calloc(count, sizeof(*graph_workspace.provenance)),
		calloc(count, sizeof(*graph_workspace.frontier)),
		calloc(count, sizeof(*graph_workspace.frontier_pending)),
		calloc(count, sizeof(*graph_workspace.tarjan_index)),
		calloc(count, sizeof(*graph_workspace.lowlink)),
		calloc(count, sizeof(*graph_workspace.on_stack)),
		calloc(count, sizeof(*graph_workspace.component_stack)),
		calloc(count, sizeof(*graph_workspace.dfs_frames)),
		calloc(count, sizeof(*graph_workspace.scc_ids)),
		calloc(count, sizeof(*graph_workspace.scc_incoming)),
		calloc(count, sizeof(*graph_workspace.group_scratch)), count
	};
	assert(graph_workspace.reachability != NULL && graph_workspace.provenance != NULL &&
			graph_workspace.frontier != NULL && graph_workspace.frontier_pending != NULL &&
			graph_workspace.tarjan_index != NULL && graph_workspace.lowlink != NULL &&
			graph_workspace.on_stack != NULL && graph_workspace.component_stack != NULL &&
			graph_workspace.dfs_frames != NULL && graph_workspace.scc_ids != NULL &&
			graph_workspace.scc_incoming != NULL && graph_workspace.group_scratch != NULL);
	input = (struct mlc_graph_input_s){&index, sources, count, NULL, 0};
	output = (struct mlc_graph_output_s){groups, count, &group_count};
	assert(mlc_graph_analyze(&input, &graph_workspace, &output) == MLC_CORE_OK);
	assert(group_count == count && groups[0].direct &&
			groups[count - 1].representative == BASE(count - 1));
	free(graph_workspace.group_scratch);
	free(graph_workspace.scc_incoming);
	free(graph_workspace.scc_ids);
	free(graph_workspace.dfs_frames);
	free(graph_workspace.component_stack);
	free(graph_workspace.on_stack);
	free(graph_workspace.lowlink);
	free(graph_workspace.tarjan_index);
	free(graph_workspace.frontier_pending);
	free(graph_workspace.frontier);
	free(graph_workspace.provenance);
	free(graph_workspace.reachability);
	free(groups);
	free(exact);
	free(order);
	free(sources);
	free(payloads);
	free(candidates);
}
