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

#include <tinyara/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <tinyara/mm/mm.h>

#include "tc_internal.h"

int run_mem_leak_checker(int checker_pid, char *bin_name);
void mem_leak_checker_set_test_observer(void (*observer)(void *allocation, bool leak));
bool tc_mem_leak_checker_realloc_fixture(void);

struct mlc_characterization_s {
	uintptr_t words[2];
};

struct mlc_observation_s {
	void *self;
	void *cycle_a;
	void *cycle_b;
	void *chain_head;
	void *chain_middle;
	void *chain_tail;
	bool self_hidden;
	bool cycle_a_hidden;
	bool cycle_b_hidden;
	bool chain_head_leak;
	bool chain_middle_hidden;
	bool chain_tail_hidden;
};

static const char *const g_mlc_task13_fixtures[] = {
	"mlc_kernel_tc_empty", "mlc_kernel_tc_rootless_self",
	"mlc_kernel_tc_rootless_two_cycle", "mlc_kernel_tc_rootless_long_cycle",
	"mlc_kernel_tc_rootless_reversed_chain", "mlc_kernel_tc_rootless_cycle_tail",
	"mlc_kernel_tc_rootless_convergence", "mlc_kernel_tc_rooted_equivalents",
	"mlc_kernel_tc_cross_domain", "mlc_kernel_tc_bounds_exact_interior_one_past",
	"mlc_kernel_tc_provenance_alignment", "mlc_kernel_tc_ambiguity_upgrade",
	"mlc_kernel_tc_iterative_scc_max_depth", "mlc_kernel_tc_exclusions",
	"mlc_kernel_tc_broad_ram_overlap", "mlc_kernel_tc_caller_boundaries",
	"mlc_kernel_tc_worker_register_irq_roots", "mlc_kernel_tc_post_unpin_poison",
	"mlc_kernel_tc_report_dumps", "mlc_kernel_tc_legacy_flat",
	"mlc_kernel_tc_legacy_loadable"
};

static void mlc_task13_fixture_manifest(void)
{
	size_t index;

	for (index = 0; index < sizeof(g_mlc_task13_fixtures) /
			sizeof(g_mlc_task13_fixtures[0]); index++) {
		printf("MLC_TASK13_FIXTURE name=%s status=deferred_host_static\n",
			g_mlc_task13_fixtures[index]);
	}
}

static struct mlc_observation_s g_mlc_observation;

static void mlc_observe_allocation(void *allocation, bool leak)
{
	if (allocation == g_mlc_observation.self) {
		g_mlc_observation.self_hidden = !leak;
	} else if (allocation == g_mlc_observation.cycle_a) {
		g_mlc_observation.cycle_a_hidden = !leak;
	} else if (allocation == g_mlc_observation.cycle_b) {
		g_mlc_observation.cycle_b_hidden = !leak;
	} else if (allocation == g_mlc_observation.chain_head) {
		g_mlc_observation.chain_head_leak = leak;
	} else if (allocation == g_mlc_observation.chain_middle) {
		g_mlc_observation.chain_middle_hidden = !leak;
	} else if (allocation == g_mlc_observation.chain_tail) {
		g_mlc_observation.chain_tail_hidden = !leak;
	}
}

static bool mlc_requested_size_is(void *allocation, size_t requested)
{
	struct mm_allocnode_s *node;
	size_t capacity;

	if (!allocation) {
		return false;
	}
	node = (struct mm_allocnode_s *)((char *)allocation - SIZEOF_MM_ALLOCNODE);
	capacity = node->size - SIZEOF_MM_ALLOCNODE;
	return node->alloc_padding <= capacity && capacity - node->alloc_padding == requested;
}

static bool mlc_alloc_bounds_fixture(void)
{
	void *allocation;
	void *zero;
	volatile size_t impossible_size = SIZE_MAX;
	uint16_t padding;
	bool passed = true;

	allocation = malloc(17);
	passed = passed && mlc_requested_size_is(allocation, 17);
	free(allocation);

	allocation = calloc(3, 7);
	passed = passed && mlc_requested_size_is(allocation, 21);
	free(allocation);

	allocation = zalloc(19);
	passed = passed && mlc_requested_size_is(allocation, 19);
	free(allocation);

	allocation = memalign(64, 33);
	passed = passed && mlc_requested_size_is(allocation, 33);
	free(allocation);

	zero = malloc(0);
	passed = passed && zero == NULL;
	free(zero);

	zero = realloc(NULL, 0);
	passed = passed && zero == NULL;
	free(zero);

	allocation = malloc(23);
	if (allocation) {
		struct mm_allocnode_s *node = (struct mm_allocnode_s *)((char *)allocation - SIZEOF_MM_ALLOCNODE);
		padding = node->alloc_padding;
		passed = passed && realloc(allocation, impossible_size) == NULL;
		passed = passed && node->alloc_padding == padding;
		free(allocation);
	} else {
		passed = false;
	}

	allocation = malloc(23);
	if (allocation) {
		passed = passed && realloc(allocation, 0) == NULL;
	} else {
		passed = false;
	}

	return passed;
}

int tc_mem_leak_checker_main(void)
{
	struct mlc_characterization_s *self;
	struct mlc_characterization_s *cycle_a;
	struct mlc_characterization_s *cycle_b;
	struct mlc_characterization_s *chain_head;
	struct mlc_characterization_s *chain_middle;
	struct mlc_characterization_s *chain_tail;
	const char *self_verdict = "unobserved";
	const char *cycle_verdict = "unobserved";
	const char *chain_verdict = "unobserved";
	bool fixture_healthy = false;
	int ret;
	bool alloc_bounds_healthy;
	bool realloc_healthy;
	mlc_task13_fixture_manifest();

	alloc_bounds_healthy = mlc_alloc_bounds_fixture();
	realloc_healthy = tc_mem_leak_checker_realloc_fixture();
	printf("MLC_QA fixture=mlc_alloc_bounds status=%s\n", alloc_bounds_healthy ? "PASS" : "FAIL");
	printf("MLC_QA fixture=mlc_alloc_zero status=%s\n", alloc_bounds_healthy ? "PASS" : "FAIL");
	printf("MLC_QA fixture=mlc_realloc_real_heap branches=same,shrink,previous,next,both,move,failure status=%s\n",
		   realloc_healthy ? "PASS" : "FAIL");

	self = (struct mlc_characterization_s *)calloc(1, sizeof(*self));
	cycle_a = (struct mlc_characterization_s *)calloc(1, sizeof(*cycle_a));
	cycle_b = (struct mlc_characterization_s *)calloc(1, sizeof(*cycle_b));
	chain_head = (struct mlc_characterization_s *)calloc(1, sizeof(*chain_head));
	chain_middle = (struct mlc_characterization_s *)calloc(1, sizeof(*chain_middle));
	chain_tail = (struct mlc_characterization_s *)calloc(1, sizeof(*chain_tail));

	if (self && cycle_a && cycle_b && chain_head && chain_middle && chain_tail) {
		self->words[0] = (uintptr_t)self;
		cycle_a->words[0] = (uintptr_t)cycle_b;
		cycle_b->words[0] = (uintptr_t)cycle_a;
		chain_head->words[0] = (uintptr_t)chain_middle;
		chain_middle->words[0] = (uintptr_t)chain_tail;
		chain_tail->words[0] = 0;
		g_mlc_observation.self = self;
		g_mlc_observation.cycle_a = cycle_a;
		g_mlc_observation.cycle_b = cycle_b;
		g_mlc_observation.chain_head = chain_head;
		g_mlc_observation.chain_middle = chain_middle;
		g_mlc_observation.chain_tail = chain_tail;
		mem_leak_checker_set_test_observer(mlc_observe_allocation);

		ret = run_mem_leak_checker(getpid(), "kernel");
		mem_leak_checker_set_test_observer(NULL);
		if (ret == OK) {
			fixture_healthy = true;
			self_verdict = g_mlc_observation.self_hidden ? "hidden" : "reported";
			cycle_verdict = g_mlc_observation.cycle_a_hidden &&
					g_mlc_observation.cycle_b_hidden ? "hidden" : "reported";
			chain_verdict = g_mlc_observation.chain_head_leak &&
					g_mlc_observation.chain_middle_hidden &&
					g_mlc_observation.chain_tail_hidden ? "reported" : "other";
		}
	}

	printf("MLC_QA fixture=mlc_characterization self=%s cycle=%s chain_only_head=%s gating=false\n",
		   self_verdict, cycle_verdict, chain_verdict);
	printf("MLC_QA fixture=mlc_bootstrap status=%s baseline_sha=c93078ab05bb6463467669fb6ee19bb75ee7eaba\n",
		   fixture_healthy ? "PASS" : "FAIL");

	free(chain_tail);
	free(chain_middle);
	free(chain_head);
	free(cycle_b);
	free(cycle_a);
	free(self);
	if (!fixture_healthy || !alloc_bounds_healthy || !realloc_healthy) {
		printf("[tc_mem_leak_checker_main] FAIL\n");
		total_fail++;
		return ERROR;
	}

	TC_SUCCESS_RESULT();
	return OK;
}
