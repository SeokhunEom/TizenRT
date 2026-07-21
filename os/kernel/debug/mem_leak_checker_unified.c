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

#include <tinyara/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mem_leak_checker_domain.h"
#include "mem_leak_checker_graph.h"
#include "mem_leak_checker_graph_internal.h"
#include "mem_leak_checker_unified.h"

#ifndef CONFIG_MEM_LEAK_CHECKER_MAX_ALLOC_COUNT
#define CONFIG_MEM_LEAK_CHECKER_MAX_ALLOC_COUNT 3000
#endif

#define MLC_UNIFIED_CAPACITY \
	(CONFIG_MEM_LEAK_CHECKER_MAX_ALLOC_COUNT * MLC_DOMAIN_HEAP_CAPACITY)
#define MLC_UNIFIED_EXACT_CAPACITY (MLC_UNIFIED_CAPACITY * 2 + 1)

static size_t g_unified_sorted[MLC_UNIFIED_CAPACITY];
static size_t g_unified_exact[MLC_UNIFIED_EXACT_CAPACITY];
static enum mlc_reachability_e g_unified_reach[MLC_UNIFIED_CAPACITY];
static uint8_t g_unified_provenance[MLC_UNIFIED_CAPACITY];
static size_t g_unified_frontier[MLC_UNIFIED_CAPACITY];
static bool g_unified_frontier_pending[MLC_UNIFIED_CAPACITY];
static size_t g_unified_tarjan_index[MLC_UNIFIED_CAPACITY];
static size_t g_unified_lowlink[MLC_UNIFIED_CAPACITY];
static bool g_unified_on_stack[MLC_UNIFIED_CAPACITY];
static size_t g_unified_component[MLC_UNIFIED_CAPACITY];
static struct mlc_tarjan_frame_s g_unified_frames[MLC_UNIFIED_CAPACITY];
static size_t g_unified_scc_ids[MLC_UNIFIED_CAPACITY];
static bool g_unified_scc_incoming[MLC_UNIFIED_CAPACITY];
static struct mlc_leak_group_s g_unified_groups[MLC_UNIFIED_CAPACITY];
static struct mlc_leak_group_s g_unified_output_groups[MLC_UNIFIED_CAPACITY];
static struct mlc_graph_root_range_s g_unified_roots[MLC_UNIFIED_CAPACITY];

struct mlc_unified_control_range_s {
	uintptr_t begin;
	size_t size;
};

static const struct mlc_unified_control_range_s g_unified_control_ranges[] = {
	{(uintptr_t)g_unified_sorted, sizeof(g_unified_sorted)},
	{(uintptr_t)g_unified_exact, sizeof(g_unified_exact)},
	{(uintptr_t)g_unified_reach, sizeof(g_unified_reach)},
	{(uintptr_t)g_unified_provenance, sizeof(g_unified_provenance)},
	{(uintptr_t)g_unified_frontier, sizeof(g_unified_frontier)},
	{(uintptr_t)g_unified_frontier_pending, sizeof(g_unified_frontier_pending)},
	{(uintptr_t)g_unified_tarjan_index, sizeof(g_unified_tarjan_index)},
	{(uintptr_t)g_unified_lowlink, sizeof(g_unified_lowlink)},
	{(uintptr_t)g_unified_on_stack, sizeof(g_unified_on_stack)},
	{(uintptr_t)g_unified_component, sizeof(g_unified_component)},
	{(uintptr_t)g_unified_frames, sizeof(g_unified_frames)},
	{(uintptr_t)g_unified_scc_ids, sizeof(g_unified_scc_ids)},
	{(uintptr_t)g_unified_scc_incoming, sizeof(g_unified_scc_incoming)},
	{(uintptr_t)g_unified_groups, sizeof(g_unified_groups)},
	{(uintptr_t)g_unified_output_groups, sizeof(g_unified_output_groups)},
	{(uintptr_t)g_unified_roots, sizeof(g_unified_roots)}
};

size_t mlc_unified_control_range_count(void)
{
	return sizeof(g_unified_control_ranges) /
		sizeof(g_unified_control_ranges[0]);
}

int mlc_unified_control_range(size_t index, uintptr_t *begin, size_t *size)
{
	if (begin == NULL || size == NULL || index >= mlc_unified_control_range_count()) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	*begin = g_unified_control_ranges[index].begin;
	*size = g_unified_control_ranges[index].size;
	return MLC_CORE_OK;
}

void mlc_unified_workspace_reset(void)
{
	memset(g_unified_sorted, 0, sizeof(g_unified_sorted));
	memset(g_unified_exact, 0, sizeof(g_unified_exact));
	memset(g_unified_reach, 0, sizeof(g_unified_reach));
	memset(g_unified_provenance, 0, sizeof(g_unified_provenance));
	memset(g_unified_frontier, 0, sizeof(g_unified_frontier));
	memset(g_unified_frontier_pending, 0, sizeof(g_unified_frontier_pending));
	memset(g_unified_tarjan_index, 0, sizeof(g_unified_tarjan_index));
	memset(g_unified_lowlink, 0, sizeof(g_unified_lowlink));
	memset(g_unified_on_stack, 0, sizeof(g_unified_on_stack));
	memset(g_unified_component, 0, sizeof(g_unified_component));
	memset(g_unified_frames, 0, sizeof(g_unified_frames));
	memset(g_unified_scc_ids, 0, sizeof(g_unified_scc_ids));
	memset(g_unified_scc_incoming, 0, sizeof(g_unified_scc_incoming));
	memset(g_unified_groups, 0, sizeof(g_unified_groups));
	memset(g_unified_output_groups, 0, sizeof(g_unified_output_groups));
	memset(g_unified_roots, 0, sizeof(g_unified_roots));
}

enum mlc_reachability_e mlc_unified_reachability(size_t index)
{
	return index < MLC_UNIFIED_CAPACITY ? g_unified_reach[index] :
		MLC_REACH_NONE;
}

int mlc_unified_analyze(const struct mlc_candidate_s *candidates,
		size_t candidate_count, const void *const *candidate_sources,
		const struct mlc_unified_root_s *roots, size_t root_count,
		bool *leak_flags, struct mlc_unified_group_s *groups,
		size_t group_capacity, size_t *group_count)
{
	struct mlc_candidate_index_s index;
	struct mlc_candidate_workspace_s index_workspace;
	struct mlc_graph_workspace_s graph_workspace;
	struct mlc_graph_input_s input;
	struct mlc_graph_output_s output;
	size_t index_value;
	size_t produced_groups = 0;
	int result;

	if (candidates == NULL || candidate_sources == NULL || leak_flags == NULL ||
			groups == NULL || group_count == NULL ||
			candidate_count > MLC_UNIFIED_CAPACITY || root_count > MLC_UNIFIED_CAPACITY ||
			group_capacity == 0 || (root_count > 0 && roots == NULL)) {
		return MLC_CORE_INVALID_ARGUMENT;
	}
	for (index_value = 0; index_value < root_count; index_value++) {
		if (roots[index_value].source == NULL && roots[index_value].source_size != 0) {
			return MLC_CORE_INVALID_ARGUMENT;
		}
		g_unified_roots[index_value] = (struct mlc_graph_root_range_s){
			roots[index_value].source, roots[index_value].source_size,
			roots[index_value].source_begin};
	}

	index_workspace = (struct mlc_candidate_workspace_s){
		g_unified_sorted, MLC_UNIFIED_CAPACITY,
		g_unified_exact, MLC_UNIFIED_EXACT_CAPACITY, NULL};
	result = mlc_candidate_index_build(&index, candidates, candidate_count,
			&index_workspace);
	if (result != MLC_CORE_OK) {
		return result;
	}
	graph_workspace = (struct mlc_graph_workspace_s){
		g_unified_reach, g_unified_provenance, g_unified_frontier,
		g_unified_frontier_pending, g_unified_tarjan_index, g_unified_lowlink,
		g_unified_on_stack, g_unified_component, g_unified_frames,
		g_unified_scc_ids, g_unified_scc_incoming, g_unified_groups,
		candidate_count};
	input = (struct mlc_graph_input_s){
		&index, candidate_sources, candidate_count,
		root_count == 0 ? NULL : g_unified_roots, root_count};
	output = (struct mlc_graph_output_s){g_unified_output_groups, candidate_count,
		&produced_groups};
	result = mlc_graph_analyze(&input, &graph_workspace, &output);
	if (result != MLC_CORE_OK || produced_groups > group_capacity) {
		return result != MLC_CORE_OK ? result : MLC_CORE_INSUFFICIENT_OUTPUT;
	}
	for (index_value = 0; index_value < candidate_count; index_value++) {
		leak_flags[index_value] =
			g_unified_reach[index_value] == MLC_REACH_NONE;
	}
	for (index_value = 0; index_value < produced_groups; index_value++) {
		groups[index_value] = (struct mlc_unified_group_s){
			g_unified_output_groups[index_value].representative,
			g_unified_output_groups[index_value].member_count,
			g_unified_output_groups[index_value].direct,
			g_unified_output_groups[index_value].strongest_provenance};
	}
	*group_count = produced_groups;
	return MLC_CORE_OK;
}
