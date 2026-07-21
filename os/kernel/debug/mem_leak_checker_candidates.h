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

#ifndef __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_CANDIDATES_H
#define __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_CANDIDATES_H

#include <stddef.h>
#include <stdint.h>

#include <tinyara/mm/mm.h>

#include "mem_leak_checker_core.h"
#include "mem_leak_checker_budget.h"

enum mlc_candidate_snapshot_result_e {
	MLC_CANDIDATE_SNAPSHOT_OK = 0,
	MLC_CANDIDATE_SNAPSHOT_INVALID = -1,
	MLC_CANDIDATE_SNAPSHOT_CAPACITY = -2,
	MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT = -3,
	MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED = -4,
	MLC_CANDIDATE_SNAPSHOT_CONTENTION = -5
};

enum mlc_exclusion_kind_e {
	MLC_EXCLUDE_CHECKER_CONTROL = 0,
	MLC_EXCLUDE_ACTIVE_TCB,
	MLC_EXCLUDE_FULL_STACK,
	MLC_EXCLUDE_LOADABLE_ROOT_CONTAINER,
	MLC_EXCLUDE_REGISTRY_LOADER,
	MLC_EXCLUDE_PAUSE_ADMISSION,
	MLC_EXCLUDE_TIMER,
	MLC_EXCLUDE_KIND_COUNT
};

enum mlc_root_kind_e {
	MLC_ROOT_BROAD_STATIC = 0,
	MLC_ROOT_ACTIVE_TCB,
	MLC_ROOT_TASK_STACK_LIVE,
	MLC_ROOT_IRQ_STACK_LIVE,
	MLC_ROOT_TASK_REGISTERS,
	MLC_ROOT_LOADABLE_WRITABLE
};

struct mlc_address_range_s {
	uintptr_t begin;
	size_t size;
};

struct mlc_exclusion_range_s {
	struct mlc_address_range_s range;
	enum mlc_exclusion_kind_e kind;
};

struct mlc_root_input_s {
	struct mlc_address_range_s range;
	enum mlc_root_kind_e kind;
};

struct mlc_loadable_mapping_input_s {
	struct mlc_address_range_s mapping;
	struct mlc_address_range_s declared_container;
};

struct mlc_candidate_exclusion_s {
	struct mlc_candidate_s allocation;
	uint32_t kind_mask;
};

struct mlc_root_range_s {
	struct mlc_address_range_s range;
	enum mlc_root_kind_e kind;
};

struct mlc_candidate_snapshot_request_s {
	struct mm_heap_s *const *heaps;
	size_t heap_count;
	const struct mlc_exclusion_range_s *exclusions;
	size_t exclusion_count;
	const struct mlc_root_input_s *roots;
	size_t root_count;
	const struct mlc_loadable_mapping_input_s *mappings;
	size_t mapping_count;
};

struct mlc_candidate_snapshot_workspace_s {
	struct mlc_candidate_s *candidates;
	size_t candidate_capacity;
	struct mlc_candidate_exclusion_s *exclusions;
	size_t exclusion_capacity;
	struct mlc_root_range_s *roots;
	size_t root_capacity;
};

struct mlc_candidate_snapshot_result_s {
	size_t allocated_count;
	size_t candidate_count;
	size_t exclusion_count;
	size_t root_count;
};

int mlc_candidate_snapshot_collect(
		const struct mlc_candidate_snapshot_request_s *request,
		const struct mlc_candidate_snapshot_workspace_s *workspace,
		struct mlc_candidate_snapshot_result_s *result);

#endif
