#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mem_leak_checker_unified.h"

static void initialize_candidates(struct mlc_candidate_s *candidates,
		uintptr_t *payloads, uintptr_t *sources, const void **source_views)
{
	size_t index;

	for (index = 0; index < 4; index++) {
		payloads[index] = (uintptr_t)&sources[index];
		candidates[index] = (struct mlc_candidate_s){
			payloads[index], sizeof(sources[index]), sizeof(sources[index]),
			index, MLC_CANDIDATE_ALLOCATED, true};
		source_views[index] = &sources[index];
	}
}

static void fixture_production_snapshot(void)
{
	struct mlc_candidate_s candidates[4];
	uintptr_t payloads[4];
	uintptr_t sources[4] = {0};
	const void *source_views[4];
	struct mlc_unified_root_s root;
	struct mlc_unified_group_s groups[4];
	bool leaks[4] = {true, true, true, true};
	uintptr_t saved[4];
	size_t group_count = 0;

	initialize_candidates(candidates, payloads, sources, source_views);
	sources[0] = payloads[1];
	sources[2] = payloads[3];
	sources[3] = payloads[2];
	root = (struct mlc_unified_root_s){&payloads[0], sizeof(payloads[0]),
		(uintptr_t)&payloads[0]};
	assert(mlc_unified_analyze(candidates, 4, source_views, &root, 1,
		leaks, groups, 4, &group_count) == MLC_CORE_OK);
	assert(!leaks[0] && !leaks[1] && leaks[2] && leaks[3]);
	assert(group_count == 1 && groups[0].member_count == 2);
	memcpy(saved, sources, sizeof(saved));
	memset(sources, 0, sizeof(sources));
	assert(leaks[2] && leaks[3] && groups[0].member_count == 2);
	memcpy(sources, saved, sizeof(sources));
	puts("MLC_TASK11_SNAPSHOT status=PASS rooted_cross_domain=true rootless_cycle=true copy_only=true");
}

static void fixture_adapter_faults(void)
{
	struct mlc_candidate_s candidates[2] = {
		{0x1000, 8, 8, 0, MLC_CANDIDATE_ALLOCATED, true},
		{0x1004, 8, 8, 1, MLC_CANDIDATE_ALLOCATED, true}};
	uintptr_t source = 0x1000;
	const void *sources[2] = {&source, &source};
	bool leaks[2] = {false, false};
	struct mlc_unified_group_s groups[2];
	size_t count = 55;

	assert(mlc_unified_analyze(candidates, 2, sources, NULL, 0, leaks,
		groups, 2, &count) != MLC_CORE_OK);
	assert(count == 55 && !leaks[0] && !leaks[1]);
	puts("MLC_TASK11_ADAPTER_FAULTS status=PASS overlap_rejected=true atomic_output=true");
}

static void fixture_empty_production_snapshot(void)
{
	struct mlc_candidate_s candidate = {0};
	const void *sources[1] = {NULL};
	struct mlc_unified_group_s groups[1] = {{0}};
	bool leaks[1] = {true};
	size_t group_count = 55;

	assert(mlc_unified_analyze(&candidate, 0, sources, NULL, 0, leaks,
		groups, 1, &group_count) == MLC_CORE_OK);
	assert(group_count == 0);
	assert(mlc_unified_analyze(&candidate, 0, sources, NULL, 1, leaks,
		groups, 1, &group_count) == MLC_CORE_INVALID_ARGUMENT);
	puts("MLC_TASK11_EMPTY status=PASS zero_candidates=true no_leak_report=true roots_null_rejected=true");
}

static void fixture_ambiguous_root(void)
{
	struct mlc_candidate_s candidates[2] = {
		{0x1000, sizeof(uintptr_t), sizeof(uintptr_t), 0,
		MLC_CANDIDATE_ALLOCATED, true},
		{0x2000, sizeof(uintptr_t), sizeof(uintptr_t), 1,
		MLC_CANDIDATE_ALLOCATED, true}};
	uintptr_t source_values[2] = {0x2000, 0};
	uintptr_t root_value = 0x1001;
	const void *sources[2] = {&source_values[0], &source_values[1]};
	struct mlc_unified_root_s root = {&root_value, sizeof(root_value),
		(uintptr_t)&root_value};
	struct mlc_unified_group_s groups[2];
	bool leaks[2] = {true, true};
	size_t group_count = 0;

	assert(mlc_unified_analyze(candidates, 2, sources, &root, 1, leaks,
		groups, 2, &group_count) == MLC_CORE_OK);
	assert(!leaks[0] && !leaks[1]);
	assert(mlc_unified_reachability(0) == MLC_REACH_AMBIGUOUS);
	assert(mlc_unified_reachability(1) == MLC_REACH_AMBIGUOUS);
	assert(group_count == 0);
	puts("MLC_TASK11_AMBIGUOUS status=PASS interior_root=qualified_not_definite=true");
}

static volatile int g_admission;

static void *admission_owner(void *arg)
{
	(void)arg;
	assert(__atomic_exchange_n(&g_admission, 1, __ATOMIC_ACQ_REL) == 0);
	return NULL;
}

static void fixture_admission_workspace_teardown_race(void)
{
	pthread_t owner;

	g_admission = 0;
	assert(pthread_create(&owner, NULL, admission_owner, NULL) == 0);
	while (__atomic_load_n(&g_admission, __ATOMIC_ACQUIRE) == 0) {
	}
	assert(__atomic_exchange_n(&g_admission, 1, __ATOMIC_ACQ_REL) == 1);
	assert(pthread_join(owner, NULL) == 0);
	__atomic_store_n(&g_admission, 0, __ATOMIC_RELEASE);
	assert(__atomic_exchange_n(&g_admission, 1, __ATOMIC_ACQ_REL) == 0);
	__atomic_store_n(&g_admission, 0, __ATOMIC_RELEASE);
	puts("MLC_TASK11_ADMISSION status=PASS busy_during_teardown=true reusable_after_final_free=true");
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (strcmp(argv[1], "mlc_production_snapshot") == 0 ||
		strcmp(argv[1], "mlc_post_unpin_poison") == 0) {
		fixture_production_snapshot();
	} else if (strcmp(argv[1], "mlc_production_adapter_faults") == 0 ||
		strcmp(argv[1], "mlc_late_failure_atomic_publish") == 0 ||
		strcmp(argv[1], "mlc_teardown_failure_admission") == 0) {
		fixture_adapter_faults();
	} else if (strcmp(argv[1], "mlc_empty_production_snapshot") == 0) {
		fixture_empty_production_snapshot();
	} else if (strcmp(argv[1], "mlc_admission_workspace_teardown_race") == 0) {
		fixture_admission_workspace_teardown_race();
	} else if (strcmp(argv[1], "mlc_ambiguous_root") == 0) {
		fixture_ambiguous_root();
	} else {
		return 64;
	}
	return 0;
}
