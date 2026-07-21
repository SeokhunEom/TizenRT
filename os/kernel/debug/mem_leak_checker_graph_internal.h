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

#ifndef __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_GRAPH_INTERNAL_H
#define __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_GRAPH_INTERNAL_H

#include "mem_leak_checker_graph.h"

int mlc_graph_next_edge(const struct mlc_graph_input_s *input,
		size_t source_index, size_t *offset, bool *found,
		struct mlc_match_s *match);
int mlc_graph_tarjan(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		struct mlc_leak_group_s *groups, size_t *group_count);
uint8_t mlc_graph_edge_provenance(const struct mlc_match_s *match);
int mlc_graph_validate(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		const struct mlc_graph_output_s *output);

#endif
