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
#include <stdio.h>
#include <string.h>

#include "mem_leak_checker_core.h"
#include "mem_leak_checker_graph.h"

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define BASE(index) ((uintptr_t)(0x1000 + (index) * 0x100))

void mlc_test_configured_max_depth(void);

struct fixture_s {
	struct mlc_candidate_s candidates[16];
	uintptr_t payloads[16][2];
	const void *sources[16];
	size_t order[16];
	size_t exact[33];
	struct mlc_candidate_index_s index;
	enum mlc_reachability_e reach[16];
	uint8_t provenance[16];
	size_t frontier[16];
	bool pending[16];
	size_t tarjan_index[16];
	size_t lowlink[16];
	bool on_stack[16];
	size_t component[16];
	struct mlc_tarjan_frame_s frames[16];
	size_t scc_ids[16];
	bool scc_incoming[16];
	struct mlc_leak_group_s group_scratch[16];
	struct mlc_leak_group_s groups[16];
};

static void fixture_init(struct fixture_s *fixture, size_t count)
{
	size_t current;
	struct mlc_candidate_workspace_s index_workspace = {
		fixture->order, COUNT(fixture->order), fixture->exact,
		COUNT(fixture->exact), NULL
	};

	memset(fixture, 0, sizeof(*fixture));
	for (current = 0; current < count; current++) {
		fixture->candidates[current] = (struct mlc_candidate_s){
			BASE(current), sizeof(fixture->payloads[current]),
			sizeof(fixture->payloads[current]), current,
			MLC_CANDIDATE_ALLOCATED, true
		};
		fixture->sources[current] = fixture->payloads[current];
	}
	assert(mlc_candidate_index_build(&fixture->index, fixture->candidates,
			count, &index_workspace) == MLC_CORE_OK);
}

static struct mlc_graph_workspace_s workspace(struct fixture_s *fixture,
		size_t capacity)
{
	return (struct mlc_graph_workspace_s){
		fixture->reach, fixture->provenance, fixture->frontier,
		fixture->pending, fixture->tarjan_index, fixture->lowlink,
		fixture->on_stack, fixture->component, fixture->frames,
		fixture->scc_ids, fixture->scc_incoming, fixture->group_scratch, capacity
	};
}

static int analyze(struct fixture_s *fixture, size_t count,
		const struct mlc_graph_root_range_s *roots, size_t root_count,
		size_t capacity, size_t group_capacity, size_t *group_count)
{
	struct mlc_graph_input_s input = {
		&fixture->index, fixture->sources, count, roots, root_count
	};
	struct mlc_graph_workspace_s graph_workspace = workspace(fixture, capacity);
	struct mlc_graph_output_s output = {
		fixture->groups, group_capacity, group_count
	};

	return mlc_graph_analyze(&input, &graph_workspace, &output);
}

static void test_rooted_reachability_and_upgrade(void)
{
	struct fixture_s fixture;
	uint8_t weak_root[sizeof(uintptr_t) + 1];
	uintptr_t strong_root = BASE(3);
	struct mlc_graph_root_range_s roots[2];
	size_t group_count = 99;

	fixture_init(&fixture, 5);
	fixture.payloads[1][0] = BASE(2);
	fixture.payloads[2][0] = BASE(4);
	fixture.payloads[3][0] = BASE(1);
	memset(weak_root, 0, sizeof(weak_root));
	memcpy(weak_root + 1, &(uintptr_t){BASE(1)}, sizeof(uintptr_t));
	roots[0] = (struct mlc_graph_root_range_s){weak_root + 1, sizeof(uintptr_t), 0x8001};
	roots[1] = (struct mlc_graph_root_range_s){&strong_root, sizeof(uintptr_t), 0x9000};

	assert(analyze(&fixture, 5, roots, COUNT(roots), 5, 5, &group_count) ==
			MLC_CORE_OK);
	assert(group_count == 1 && fixture.groups[0].representative == BASE(0));
	assert(fixture.reach[1] == MLC_REACH_STRONG);
	assert(fixture.reach[2] == MLC_REACH_STRONG);
	assert(fixture.reach[3] == MLC_REACH_STRONG);
	assert(fixture.reach[4] == MLC_REACH_STRONG);
	assert((fixture.provenance[1] & MLC_PROVENANCE_ALIGNED_EXACT) != 0);
}

static void test_rootless_groups(void)
{
	struct fixture_s fixture;
	size_t group_count = 0;

	fixture_init(&fixture, 6);
	fixture.payloads[0][0] = BASE(0);
	fixture.payloads[1][0] = BASE(2);
	fixture.payloads[2][0] = BASE(1);
	fixture.payloads[3][0] = BASE(4) + 1;
	memcpy((uint8_t *)fixture.payloads[4] + 1, &(uintptr_t){BASE(5)},
			sizeof(uintptr_t));

	assert(analyze(&fixture, 6, NULL, 0, 6, 6, &group_count) == MLC_CORE_OK);
	assert(group_count == 5);
	assert(fixture.groups[0].representative == BASE(0));
	assert(fixture.groups[0].member_count == 1 && fixture.groups[0].direct);
	assert(fixture.groups[1].representative == BASE(1));
	assert(fixture.groups[1].member_count == 2 && fixture.groups[1].direct);
	assert(fixture.groups[2].representative == BASE(3));
	assert(fixture.groups[2].member_count == 1 && fixture.groups[2].direct);
	assert(fixture.groups[3].representative == BASE(4));
	assert(fixture.groups[3].member_count == 1 && !fixture.groups[3].direct);
	assert((fixture.groups[3].strongest_provenance & MLC_PROVENANCE_INTERIOR) != 0);
	assert(fixture.groups[4].representative == BASE(5));
	assert(fixture.groups[4].member_count == 1 && !fixture.groups[4].direct);
	assert((fixture.groups[4].strongest_provenance & MLC_PROVENANCE_UNALIGNED) != 0);
}

static void test_cross_heap_root_chain(void)
{
	struct fixture_s fixture;
	uintptr_t root = BASE(2);
	struct mlc_graph_root_range_s roots = {
		&root, sizeof(root), 0x8100
	};
	size_t group_count = 0;

	fixture_init(&fixture, 4);
	fixture.payloads[2][0] = BASE(0);
	fixture.payloads[0][0] = BASE(3);
	fixture.payloads[1][0] = BASE(1);
	assert(analyze(&fixture, 4, &roots, 1, 4, 4, &group_count) ==
		MLC_CORE_OK);
	assert(fixture.reach[2] == MLC_REACH_STRONG &&
		fixture.reach[0] == MLC_REACH_STRONG &&
		fixture.reach[3] == MLC_REACH_STRONG &&
		fixture.reach[1] == MLC_REACH_NONE);
	assert(group_count == 1 && fixture.groups[0].representative == BASE(1) &&
		fixture.groups[0].member_count == 1);
}

static void test_zero_identity(void)
{
	struct fixture_s fixture;
	uintptr_t root = BASE(0);
	struct mlc_graph_root_range_s range = {&root, sizeof(root), 0x8000};
	size_t group_count = 0;

	fixture_init(&fixture, 2);
	fixture.candidates[0].content_size = 0;
	assert(mlc_candidate_index_build(&fixture.index, fixture.candidates, 2,
			&(struct mlc_candidate_workspace_s){fixture.order, COUNT(fixture.order),
			fixture.exact, COUNT(fixture.exact), NULL}) == MLC_CORE_OK);
	assert(analyze(&fixture, 2, &range, 1, 2, 2, &group_count) == MLC_CORE_OK);
	assert(fixture.reach[0] == MLC_REACH_STRONG);
	assert(group_count == 1 && fixture.groups[0].representative == BASE(1));
}

static void test_capacity_is_atomic(void)
{
	struct fixture_s fixture;
	struct mlc_leak_group_s before[COUNT(fixture.groups)];
	size_t group_count = 73;

	fixture_init(&fixture, 4);
	memset(fixture.groups, 0xa5, sizeof(fixture.groups));
	memcpy(before, fixture.groups, sizeof(before));
	assert(analyze(&fixture, 4, NULL, 0, 3, 4, &group_count) ==
			MLC_CORE_INSUFFICIENT_WORKSPACE);
	assert(group_count == 73);
	assert(memcmp(before, fixture.groups, sizeof(before)) == 0);
	assert(analyze(&fixture, 4, NULL, 0, 4, 3, &group_count) ==
			MLC_CORE_INSUFFICIENT_OUTPUT);
	assert(group_count == 73);
	assert(memcmp(before, fixture.groups, sizeof(before)) == 0);
}

static void test_malformed_graph_is_atomic(void)
{
	struct fixture_s fixture;
	struct mlc_graph_workspace_s graph_workspace;
	struct mlc_graph_input_s input;
	struct mlc_graph_output_s output;
	struct mlc_leak_group_s before[COUNT(fixture.groups)];
	struct mlc_graph_root_range_s bad_root = {
		&(uintptr_t){BASE(0)}, sizeof(uintptr_t), UINTPTR_MAX
	};
	size_t group_count = 61;

	fixture_init(&fixture, 2);
	memset(fixture.groups, 0x3c, sizeof(fixture.groups));
	memcpy(before, fixture.groups, sizeof(before));
	graph_workspace = workspace(&fixture, 2);
	input = (struct mlc_graph_input_s){
		&fixture.index, fixture.sources, 2, &bad_root, 1
	};
	output = (struct mlc_graph_output_s){fixture.groups, 2, &group_count};
	assert(mlc_graph_analyze(&input, &graph_workspace, &output) ==
			MLC_CORE_INVALID_RANGE);
	assert(group_count == 61 && memcmp(before, fixture.groups, sizeof(before)) == 0);
	input.roots = NULL;
	input.root_count = 0;
	graph_workspace.group_scratch = fixture.groups;
	assert(mlc_graph_analyze(&input, &graph_workspace, &output) ==
			MLC_CORE_ALIASING_WORKSPACE);
	assert(group_count == 61 && memcmp(before, fixture.groups, sizeof(before)) == 0);
}

static void test_max_depth(size_t count)
{
	struct fixture_s fixture;
	size_t group_count = 0;
	size_t current;

	assert(count <= COUNT(fixture.candidates));
	fixture_init(&fixture, count);
	for (current = 0; current + 1 < count; current++) {
		fixture.payloads[current][0] = BASE(current + 1);
	}
	assert(analyze(&fixture, count, NULL, 0, count, count, &group_count) ==
			MLC_CORE_OK);
	assert(group_count == count);
	assert(fixture.groups[0].direct);
	for (current = 1; current < count; current++) {
		assert(!fixture.groups[current].direct);
	}
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		return 64;
	}
	if (strcmp(argv[1], "mlc_graph_core") == 0) {
		test_rooted_reachability_and_upgrade();
		test_rootless_groups();
		test_cross_heap_root_chain();
		puts("MLC_HOST fixture=mlc_graph_core status=PASS");
		return 0;
	}
	if (strcmp(argv[1], "mlc_zero_graph") == 0) {
		test_zero_identity();
		puts("MLC_HOST fixture=mlc_zero_graph status=PASS");
		return 0;
	}
	if (strcmp(argv[1], "mlc_cross_heap_root_chain") == 0) {
		test_cross_heap_root_chain();
		puts("MLC_HOST fixture=mlc_cross_heap_root_chain status=PASS");
		return 0;
	}
	if (strcmp(argv[1], "mlc_frontier_tarjan_exhaustion") == 0) {
		test_capacity_is_atomic();
		test_malformed_graph_is_atomic();
		puts("MLC_HOST fixture=mlc_frontier_tarjan_exhaustion status=PASS verdict=INCOMPLETE_CAPACITY rows=0 canaries=intact");
		return 0;
	}
	if (strcmp(argv[1], "mlc_tarjan_max_depth") == 0) {
		test_max_depth(16);
		mlc_test_configured_max_depth();
		puts("MLC_HOST fixture=mlc_tarjan_max_depth status=PASS depth=3000 recursion=none");
		return 0;
	}
	return 64;
}
