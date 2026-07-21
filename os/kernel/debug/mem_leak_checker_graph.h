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

#ifndef __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_GRAPH_H
#define __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_GRAPH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mem_leak_checker_core.h"

#define MLC_PROVENANCE_ALIGNED_EXACT 0x01
#define MLC_PROVENANCE_INTERIOR 0x02
#define MLC_PROVENANCE_UNALIGNED 0x04

enum mlc_reachability_e {
	MLC_REACH_NONE = 0,
	MLC_REACH_AMBIGUOUS,
	MLC_REACH_STRONG
};

struct mlc_graph_root_range_s {
	const void *source;
	size_t source_size;
	uintptr_t source_begin;
};

struct mlc_tarjan_frame_s {
	size_t node;
	size_t next_offset;
};

struct mlc_graph_input_s {
	const struct mlc_candidate_index_s *index;
	const void *const *candidate_sources;
	size_t candidate_source_count;
	const struct mlc_graph_root_range_s *roots;
	size_t root_count;
};

struct mlc_graph_workspace_s {
	enum mlc_reachability_e *reachability;
	uint8_t *provenance;
	size_t *frontier;
	bool *frontier_pending;
	size_t *tarjan_index;
	size_t *lowlink;
	bool *on_stack;
	size_t *component_stack;
	struct mlc_tarjan_frame_s *dfs_frames;
	size_t *scc_ids;
	bool *scc_incoming;
	struct mlc_leak_group_s *group_scratch;
	size_t capacity;
};

struct mlc_leak_group_s {
	uintptr_t representative;
	size_t member_count;
	bool direct;
	uint8_t strongest_provenance;
};

struct mlc_graph_output_s {
	struct mlc_leak_group_s *groups;
	size_t group_capacity;
	size_t *group_count;
};

int mlc_graph_analyze(const struct mlc_graph_input_s *input,
		const struct mlc_graph_workspace_s *workspace,
		const struct mlc_graph_output_s *output);

#endif
