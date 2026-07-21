#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mem_leak_checker_candidates.h"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define HEAP_BYTES 1024

struct fixture_s {
	union {
		max_align_t align;
		unsigned char bytes[HEAP_BYTES];
	} storage;
	struct mm_heap_s heap;
	struct mm_heap_s *heaps[1];
	struct mlc_candidate_s candidates[16];
	struct mlc_candidate_exclusion_s manifest[16];
	struct mlc_root_range_s roots[32];
	struct mlc_candidate_snapshot_workspace_s workspace;
	struct mlc_candidate_snapshot_request_s request;
	struct mlc_candidate_snapshot_result_s result;
	struct mm_allocnode_s *nodes[8];
	struct mlc_budget_counters_s budget;
	size_t node_count;
};

struct output_image_s {
	struct mlc_candidate_s candidates[16];
	struct mlc_candidate_exclusion_s manifest[16];
	struct mlc_root_range_s roots[32];
	struct mlc_candidate_snapshot_result_s result;
};

static void capture_output(const struct fixture_s *fixture,
		struct output_image_s *image)
{
	memcpy(image->candidates, fixture->candidates, sizeof(image->candidates));
	memcpy(image->manifest, fixture->manifest, sizeof(image->manifest));
	memcpy(image->roots, fixture->roots, sizeof(image->roots));
	image->result = fixture->result;
}

static void assert_output_unchanged(const struct fixture_s *fixture,
		const struct output_image_s *image)
{
	assert(memcmp(image->candidates, fixture->candidates,
		sizeof(image->candidates)) == 0);
	assert(memcmp(image->manifest, fixture->manifest,
		sizeof(image->manifest)) == 0);
	assert(memcmp(image->roots, fixture->roots, sizeof(image->roots)) == 0);
	assert(memcmp(&image->result, &fixture->result,
		sizeof(image->result)) == 0);
}

static struct mlc_address_range_s range_of(uintptr_t begin, size_t size)
{
	struct mlc_address_range_s range = { begin, size };

	return range;
}

static void fixture_init(struct fixture_s *fixture, const size_t *requests,
		size_t count)
{
	struct mm_allocnode_s *node;
	size_t offset = 0;
	size_t previous = SIZEOF_MM_ALLOCNODE;
	size_t index;

	memset(fixture, 0, sizeof(*fixture));
	assert(mlc_budget_counters_init(&fixture->budget) == 0);
	mlc_budget_bind(&fixture->budget);
	node = (struct mm_allocnode_s *)(fixture->storage.bytes + offset);
	node->preceding = MM_ALLOC_BIT;
	node->size = SIZEOF_MM_ALLOCNODE;
	fixture->heap.mm_heapstart[0] = node;
	offset += node->size;
	for (index = 0; index < count; index++) {
		size_t capacity = (requests[index] + 15u) & ~(size_t)15u;

		node = (struct mm_allocnode_s *)(fixture->storage.bytes + offset);
		node->preceding = previous | MM_ALLOC_BIT;
		node->size = SIZEOF_MM_ALLOCNODE + capacity;
		node->alloc_padding = (uint16_t)(capacity - requests[index]);
		node->pid = (pid_t)(index + 1);
		fixture->nodes[index] = node;
		previous = node->size;
		offset += node->size;
	}
	node = (struct mm_allocnode_s *)(fixture->storage.bytes + offset);
	node->preceding = previous | MM_ALLOC_BIT;
	node->size = SIZEOF_MM_ALLOCNODE;
	fixture->heap.mm_heapend[0] = node;
	fixture->heap.mm_holder = getpid();
	fixture->heap.mm_counts_held = 1;
	fixture->heaps[0] = &fixture->heap;
	fixture->node_count = count;
	fixture->workspace.candidates = fixture->candidates;
	fixture->workspace.candidate_capacity = ARRAY_SIZE(fixture->candidates);
	fixture->workspace.exclusions = fixture->manifest;
	fixture->workspace.exclusion_capacity = ARRAY_SIZE(fixture->manifest);
	fixture->workspace.roots = fixture->roots;
	fixture->workspace.root_capacity = ARRAY_SIZE(fixture->roots);
	fixture->request.heaps = fixture->heaps;
	fixture->request.heap_count = ARRAY_SIZE(fixture->heaps);
}

static uintptr_t payload(const struct fixture_s *fixture, size_t index)
{
	return (uintptr_t)fixture->nodes[index] + SIZEOF_MM_ALLOCNODE;
}

static void assert_equation(const struct fixture_s *fixture)
{
	assert(fixture->result.allocated_count ==
		fixture->result.candidate_count + fixture->result.exclusion_count);
}

static void fixture_candidate_manifest(void)
{
	const size_t requests[] = { 64, 128, 96, 80, 48 };
	struct fixture_s fixture;
	struct mlc_exclusion_range_s exclusions[4];
	struct mlc_root_input_s roots[2];
	int result;

	fixture_init(&fixture, requests, ARRAY_SIZE(requests));
	exclusions[0] = (struct mlc_exclusion_range_s){
		range_of(payload(&fixture, 0), requests[0]), MLC_EXCLUDE_ACTIVE_TCB };
	exclusions[1] = (struct mlc_exclusion_range_s){
		range_of(payload(&fixture, 1), requests[1]), MLC_EXCLUDE_FULL_STACK };
	exclusions[2] = (struct mlc_exclusion_range_s){
		range_of(payload(&fixture, 1) + 8, 24), MLC_EXCLUDE_CHECKER_CONTROL };
	exclusions[3] = (struct mlc_exclusion_range_s){
		range_of(payload(&fixture, 3), requests[3]), MLC_EXCLUDE_TIMER };
	fixture.request.exclusions = exclusions;
	fixture.request.exclusion_count = ARRAY_SIZE(exclusions);
	roots[0] = (struct mlc_root_input_s){ exclusions[0].range,
		MLC_ROOT_ACTIVE_TCB };
	roots[1] = (struct mlc_root_input_s){
		range_of(exclusions[1].range.begin + 32, 64),
		MLC_ROOT_TASK_STACK_LIVE };
	fixture.request.roots = roots;
	fixture.request.root_count = ARRAY_SIZE(roots);
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_OK);
	assert(fixture.result.allocated_count == 5);
	assert(fixture.result.exclusion_count == 3);
	assert(fixture.result.candidate_count == 2);
	assert(fixture.manifest[1].kind_mask & (1u << MLC_EXCLUDE_FULL_STACK));
	assert(fixture.manifest[1].kind_mask & (1u << MLC_EXCLUDE_CHECKER_CONTROL));
	assert_equation(&fixture);
	fixture.workspace.exclusions = (struct mlc_candidate_exclusion_s *)
		fixture.workspace.candidates;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_INVALID);
}

static void fixture_control_source_target_exclusions(void)
{
	const size_t requests[] = { 96, 64, 64, 48 };
	struct fixture_s fixture;
	struct mlc_exclusion_range_s exclusions[3];
	struct mlc_root_input_s roots[2];
	uintptr_t broad_begin;
	int result;

	fixture_init(&fixture, requests, ARRAY_SIZE(requests));
	broad_begin = (uintptr_t)fixture.storage.bytes;
	exclusions[0] = (struct mlc_exclusion_range_s){
		range_of(payload(&fixture, 0), 32), MLC_EXCLUDE_REGISTRY_LOADER };
	exclusions[1] = (struct mlc_exclusion_range_s){
		range_of(payload(&fixture, 1), requests[1]), MLC_EXCLUDE_PAUSE_ADMISSION };
	exclusions[2] = (struct mlc_exclusion_range_s){
		range_of(payload(&fixture, 2), requests[2]), MLC_EXCLUDE_ACTIVE_TCB };
	roots[0] = (struct mlc_root_input_s){
		range_of(broad_begin,
			(uintptr_t)fixture.heap.mm_heapend[0] + SIZEOF_MM_ALLOCNODE -
			broad_begin), MLC_ROOT_BROAD_STATIC };
	roots[1] = (struct mlc_root_input_s){
		exclusions[2].range, MLC_ROOT_ACTIVE_TCB };
	fixture.request.exclusions = exclusions;
	fixture.request.exclusion_count = ARRAY_SIZE(exclusions);
	fixture.request.roots = roots;
	fixture.request.root_count = ARRAY_SIZE(roots);
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_OK);
	assert(fixture.result.root_count == 1);
	assert(fixture.roots[0].kind == MLC_ROOT_ACTIVE_TCB);
	assert(fixture.roots[0].range.begin == payload(&fixture, 2));
	assert(fixture.result.exclusion_count == 3);
	assert(fixture.result.candidate_count == 1);
	assert_equation(&fixture);
}

static void fixture_heap_backed_loadable_root(void)
{
	const size_t requests[] = { 128, 64, 64 };
	struct fixture_s fixture;
	struct mlc_loadable_mapping_input_s mappings[2];
	int result;

	fixture_init(&fixture, requests, ARRAY_SIZE(requests));
	mappings[0].mapping = range_of(payload(&fixture, 0) + 16, 32);
	mappings[0].declared_container = range_of(payload(&fixture, 0), requests[0]);
	mappings[1] = mappings[0];
	fixture.request.mappings = mappings;
	fixture.request.mapping_count = ARRAY_SIZE(mappings);
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_OK);
	assert(fixture.result.exclusion_count == 1);
	assert(fixture.result.candidate_count == 2);
	assert(fixture.result.root_count == 1);
	assert(fixture.roots[0].kind == MLC_ROOT_LOADABLE_WRITABLE);
	assert(fixture.roots[0].range.begin == mappings[0].mapping.begin);
	assert(fixture.roots[0].range.size == mappings[0].mapping.size);
	assert(fixture.candidates[0].payload_begin == payload(&fixture, 1));
	assert(fixture.candidates[1].payload_begin == payload(&fixture, 2));
	assert_equation(&fixture);
}

static void fixture_candidate_corruption_and_lock_contention(void)
{
	const size_t requests[] = { 64, 64 };
	struct fixture_s fixture;
	struct mlc_candidate_snapshot_result_s before;
	struct mm_heap_s duplicate;
	struct mm_heap_s *overlap_heaps[2];
	struct mlc_candidate_s *saved_candidates;
	unsigned char saved_storage[HEAP_BYTES];
	struct mm_heap_s saved_heap;
	size_t saved_size;
	uint16_t saved_padding;
	int result;

	fixture_init(&fixture, requests, ARRAY_SIZE(requests));
	fixture.heap.mm_holder = getpid() + 1;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_CONTENTION);
	assert(fixture.result.candidate_count == 0);
	fixture.heap.mm_holder = getpid();
	before = fixture.result;
	fixture.nodes[1]->preceding ^= 16u;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT);
	assert(fixture.result.candidate_count == before.candidate_count);
	assert(fixture.result.exclusion_count == before.exclusion_count);
	fixture.nodes[1]->preceding ^= 16u;
	saved_size = fixture.nodes[0]->size;
	fixture.nodes[0]->size = 0;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT);
	fixture.nodes[0]->size = saved_size;
	saved_padding = fixture.nodes[0]->alloc_padding;
	fixture.nodes[0]->alloc_padding = UINT16_MAX;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT);
	fixture.nodes[0]->alloc_padding = saved_padding;
	fixture.workspace.candidate_capacity = 1;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_CAPACITY);
	fixture.workspace.candidate_capacity = ARRAY_SIZE(fixture.candidates);
	duplicate = fixture.heap;
	if ((uintptr_t)&fixture.heap < (uintptr_t)&duplicate) {
		overlap_heaps[0] = &fixture.heap;
		overlap_heaps[1] = &duplicate;
	} else {
		overlap_heaps[0] = &duplicate;
		overlap_heaps[1] = &fixture.heap;
	}
	fixture.request.heaps = overlap_heaps;
	fixture.request.heap_count = ARRAY_SIZE(overlap_heaps);
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT);
	fixture.request.heaps = fixture.heaps;
	fixture.request.heap_count = ARRAY_SIZE(fixture.heaps);
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_OK);
	assert_equation(&fixture);
	memcpy(saved_storage, fixture.storage.bytes, sizeof(saved_storage));
	saved_candidates = fixture.workspace.candidates;
	fixture.workspace.candidates =
		(struct mlc_candidate_s *)fixture.storage.bytes;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_INVALID);
	assert(memcmp(saved_storage, fixture.storage.bytes,
		sizeof(saved_storage)) == 0);
	fixture.workspace.candidates = saved_candidates;
	saved_heap = fixture.heap;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace,
		(struct mlc_candidate_snapshot_result_s *)&fixture.heap);
	assert(result == MLC_CANDIDATE_SNAPSHOT_INVALID);
	assert(memcmp(&saved_heap, &fixture.heap, sizeof(saved_heap)) == 0);
}

static void fixture_tcb_stack_root_policy(void)
{
	const size_t requests[] = { 64, 128, 80 };
	struct fixture_s fixture;
	struct mlc_exclusion_range_s exclusions[2];
	struct mlc_root_input_s roots[2];
	int result;

	fixture_init(&fixture, requests, ARRAY_SIZE(requests));
	exclusions[0] = (struct mlc_exclusion_range_s){
		range_of(payload(&fixture, 0), requests[0]), MLC_EXCLUDE_ACTIVE_TCB };
	exclusions[1] = (struct mlc_exclusion_range_s){
		range_of(payload(&fixture, 1), requests[1]), MLC_EXCLUDE_FULL_STACK };
	roots[0] = (struct mlc_root_input_s){
		range_of(payload(&fixture, 0), requests[0]), MLC_ROOT_ACTIVE_TCB };
	roots[1] = (struct mlc_root_input_s){
		range_of(payload(&fixture, 1) + 48, 80), MLC_ROOT_TASK_STACK_LIVE };
	fixture.request.exclusions = exclusions;
	fixture.request.exclusion_count = ARRAY_SIZE(exclusions);
	fixture.request.roots = roots;
	fixture.request.root_count = ARRAY_SIZE(roots);
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_OK);
	assert(fixture.result.candidate_count == 1);
	assert(fixture.result.exclusion_count == 2);
	assert(fixture.result.root_count == 2);
	assert(fixture.roots[1].range.begin == payload(&fixture, 1) + 48);
	assert(fixture.roots[1].range.size == 80);
	assert_equation(&fixture);
	roots[1].range.begin = payload(&fixture, 1) - 8;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED);
	roots[1].range = range_of(payload(&fixture, 1) + 48, 80);
	exclusions[1].range = range_of(0x1000u, 0x100u);
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED);
	roots[1].range = range_of(0x1040u, 32);
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_OK);
	assert(fixture.result.allocated_count == 3);
	assert(fixture.result.exclusion_count == 1);
	assert(fixture.result.candidate_count == 2);
}

static void fixture_invalid_loadable_root_container(void)
{
	const size_t requests[] = { 65, 64 };
	struct fixture_s fixture;
	struct mlc_loadable_mapping_input_s mappings[2];
	struct output_image_s image;
	int result;

	fixture_init(&fixture, requests, ARRAY_SIZE(requests));
	memset(fixture.candidates, 0xa5, sizeof(fixture.candidates));
	memset(fixture.manifest, 0x5a, sizeof(fixture.manifest));
	memset(fixture.roots, 0x3c, sizeof(fixture.roots));
	memset(&fixture.result, 0xc3, sizeof(fixture.result));
	mappings[0].mapping = range_of(payload(&fixture, 0) + 48, 32);
	mappings[0].declared_container = range_of(payload(&fixture, 0), requests[0]);
	fixture.request.mappings = mappings;
	fixture.request.mapping_count = 1;
	capture_output(&fixture, &image);
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED);
	assert_output_unchanged(&fixture, &image);
	mappings[0].mapping = range_of(payload(&fixture, 0) + 16, 32);
	mappings[0].declared_container = range_of(payload(&fixture, 0), requests[0] + 1);
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED);
	assert_output_unchanged(&fixture, &image);
	mappings[0].declared_container = range_of(payload(&fixture, 0), requests[0]);
	mappings[1] = mappings[0];
	mappings[1].mapping.begin += 8;
	fixture.request.mapping_count = 2;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED);
	assert_output_unchanged(&fixture, &image);
	mappings[0].mapping = range_of(payload(&fixture, 0) + requests[0], 8);
	fixture.request.mapping_count = 1;
	result = mlc_candidate_snapshot_collect(&fixture.request,
		&fixture.workspace, &fixture.result);
	assert(result == MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED);
	assert_output_unchanged(&fixture, &image);
	{
		struct mlc_root_input_s roots[1];

		mappings[0].mapping = range_of(payload(&fixture, 0) + 16, 32);
		mappings[0].declared_container =
			range_of(payload(&fixture, 0), requests[0]);
		roots[0] = (struct mlc_root_input_s){ mappings[0].mapping,
			MLC_ROOT_BROAD_STATIC };
		fixture.request.roots = roots;
		fixture.request.root_count = ARRAY_SIZE(roots);
		fixture.request.mapping_count = 1;
		capture_output(&fixture, &image);
		result = mlc_candidate_snapshot_collect(&fixture.request,
			&fixture.workspace, &fixture.result);
		assert(result == MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED);
		assert_output_unchanged(&fixture, &image);
	}
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		return 64;
	}
	if (strcmp(argv[1], "mlc_candidate_manifest") == 0) {
		fixture_candidate_manifest();
	} else if (strcmp(argv[1], "mlc_control_source_target_exclusions") == 0) {
		fixture_control_source_target_exclusions();
	} else if (strcmp(argv[1], "mlc_heap_backed_loadable_root") == 0) {
		fixture_heap_backed_loadable_root();
	} else if (strcmp(argv[1], "mlc_candidate_corruption_and_lock_contention") == 0) {
		fixture_candidate_corruption_and_lock_contention();
	} else if (strcmp(argv[1], "mlc_tcb_stack_root_policy") == 0) {
		fixture_tcb_stack_root_policy();
	} else if (strcmp(argv[1], "mlc_invalid_loadable_root_container") == 0) {
		fixture_invalid_loadable_root_container();
	} else {
		return 64;
	}
	printf("MLC_TASK7_FIXTURE name=%s status=PASS\n", argv[1]);
	return 0;
}
