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

#ifndef __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_UNIFIED_H
#define __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_UNIFIED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mem_leak_checker_core.h"
#include "mem_leak_checker_graph.h"

struct mlc_unified_root_s {
	const void *source;
	size_t source_size;
	uintptr_t source_begin;
};

struct mlc_unified_group_s {
	uintptr_t representative;
	size_t member_count;
	bool direct;
	uint8_t strongest_provenance;
};

size_t mlc_unified_control_range_count(void);
int mlc_unified_control_range(size_t index, uintptr_t *begin, size_t *size);
void mlc_unified_workspace_reset(void);
enum mlc_reachability_e mlc_unified_reachability(size_t index);

int mlc_unified_analyze(const struct mlc_candidate_s *candidates,
		size_t candidate_count, const void *const *candidate_sources,
		const struct mlc_unified_root_s *roots, size_t root_count,
		bool *leak_flags, struct mlc_unified_group_s *groups,
		size_t group_capacity, size_t *group_count);

#endif
