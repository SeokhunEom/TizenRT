/****************************************************************************
 *
 * Copyright 2023 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <errno.h>
#include <stdlib.h>
#include <debug.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <queue.h>
#include <sys/types.h>
#include <tinyara/mm/mm.h>
#include <tinyara/mm/heap_regioninfo.h>
#include <tinyara/arch.h>
#include <tinyara/board.h>
#include <tinyara/compiler.h>
#include <arch/chip/memory_region.h>
#include <tinyara/binfmt/elf.h>
#include <arch/irq.h>

#include "binary_manager/binary_manager_internal.h"
#include "sched/sched.h"
#include "mem_leak_checker_domain.h"
#include "mem_leak_checker_lifecycle.h"
#include "mem_leak_checker_roots.h"
#include "mem_leak_checker_candidates.h"
#include "mem_leak_checker_candidates_internal.h"
#include "mem_leak_checker_pause.h"
#include "mem_leak_checker_unified.h"
#include "mem_leak_checker_graph.h"
#include "mem_leak_checker_report.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define CMN_BIN_IDX 0

#define MAX_ALLOC_COUNT    CONFIG_MEM_LEAK_CHECKER_MAX_ALLOC_COUNT
#define HASH_SIZE          CONFIG_MEM_LEAK_CHECKER_HASH_TABLE_SIZE
#define MEM_DUMP_MAX_BYTES 32
#define MLC_REPORT_NAME_MAX 16
#define MLC_REPORT_ROW_CAPACITY_FACTOR 2u

#ifdef CONFIG_MAX_TASKS
#define MLC_SNAPSHOT_TASK_CAPACITY CONFIG_MAX_TASKS
#else
#define MLC_SNAPSHOT_TASK_CAPACITY 64
#endif
#define MLC_SNAPSHOT_HEAP_CAPACITY MLC_DOMAIN_HEAP_CAPACITY
#define MLC_SNAPSHOT_ALLOC_CAPACITY \
	(MAX_ALLOC_COUNT * MLC_SNAPSHOT_HEAP_CAPACITY)
#define MLC_SNAPSHOT_FIXED_EXCLUSION_CAPACITY 32u
#define MLC_SNAPSHOT_EXCLUSION_CAPACITY \
	(MLC_SNAPSHOT_ALLOC_CAPACITY + MLC_SNAPSHOT_TASK_CAPACITY * 2 + \
	 MLC_SNAPSHOT_FIXED_EXCLUSION_CAPACITY)
#define MLC_SNAPSHOT_ROOT_CAPACITY \
	(MLC_SNAPSHOT_EXCLUSION_CAPACITY + MEM_VAR_REGION_COUNT + \
	 MLC_SNAPSHOT_TASK_CAPACITY * 2 + 16)
#define MLC_SNAPSHOT_MAPPING_CAPACITY MLC_DOMAIN_PIN_CAPACITY * 16
#define MLC_NODE_INFO_CAPACITY MLC_SNAPSHOT_ALLOC_CAPACITY

#define MM_PREV_NODE_SIZE(x)            ((x)->preceding & ~MM_ALLOC_BIT)

static void mlc_fatal_stop(enum mlc_fatal_reason_e reason) noreturn_function;

static void mlc_snapshot_fatal(enum mlc_incomplete_reason_e reason, void *arg)
{
	(void)reason;
	(void)arg;
	PANIC();
}

enum mlc_fatal_reset_status_e {
	MLC_RESET_RESUME_AMBIGUOUS = 0x4d4c0101,
	MLC_RESET_CANCEL_AMBIGUOUS = 0x4d4c0102,
	MLC_RESET_REMOTE_COUNTER_EXHAUSTED = 0x4d4c0103,
	MLC_RESET_CLOCK_INVALID = 0x4d4c0104,
	MLC_RESET_MAILBOX_PROTOCOL = 0x4d4c0105
};

static int mlc_fatal_reset_status(enum mlc_fatal_reason_e reason)
{
	switch (reason) {
	case MLC_PAUSE_FATAL_RESUME_AMBIGUOUS:
		return MLC_RESET_RESUME_AMBIGUOUS;
	case MLC_PAUSE_FATAL_CANCEL_AMBIGUOUS:
		return MLC_RESET_CANCEL_AMBIGUOUS;
	case MLC_PAUSE_FATAL_REMOTE_COUNTER_EXHAUSTED:
		return MLC_RESET_REMOTE_COUNTER_EXHAUSTED;
	case MLC_PAUSE_FATAL_CLOCK_INVALID:
		return MLC_RESET_CLOCK_INVALID;
	default:
		return MLC_RESET_MAILBOX_PROTOCOL;
	}
}

#ifdef CONFIG_TC_KERNEL_MEM_LEAK_CHECKER_FATAL
static const char *mlc_fatal_token(enum mlc_fatal_reason_e reason)
{
	switch (reason) {
	case MLC_PAUSE_FATAL_RESUME_AMBIGUOUS:
		return "MLC_FATAL:RESUME_AMBIGUOUS\n";
	case MLC_PAUSE_FATAL_CANCEL_AMBIGUOUS:
		return "MLC_FATAL:CANCEL_AMBIGUOUS\n";
	case MLC_PAUSE_FATAL_REMOTE_COUNTER_EXHAUSTED:
		return "MLC_FATAL:REMOTE_COUNTER_EXHAUSTED\n";
	case MLC_PAUSE_FATAL_CLOCK_INVALID:
		return "MLC_FATAL:CLOCK_INVALID\n";
	default:
		return "MLC_FATAL:MAILBOX_PROTOCOL\n";
	}
}
#endif

static void mlc_fatal_stop(enum mlc_fatal_reason_e reason)
{
#ifdef CONFIG_TC_KERNEL_MEM_LEAK_CHECKER_FATAL
	const char *marker = mlc_fatal_token(reason);

	while (*marker != '\0') {
		extern void up_lowputc(char ch);
		up_lowputc(*marker++);
	}
	(void)irqsave();
	for (;;) {
	}
#else
	board_reset(mlc_fatal_reset_status(reason));
	__builtin_unreachable();
#endif
}

void mlc_pause_fatal_dispatch(enum mlc_fatal_reason_e reason)
{
	mlc_fatal_stop(reason);
}

struct alloc_node_info_s {
	volatile struct mm_allocnode_s *node;
	struct alloc_node_info_s *next;
	uint8_t state;
};

static struct alloc_node_info_s **g_hash_table;
static struct alloc_node_info_s *g_node_info;

static struct mlc_candidate_s
	g_candidate_snapshot[MLC_SNAPSHOT_ALLOC_CAPACITY];
static struct mlc_candidate_exclusion_s
	g_candidate_exclusions[MLC_SNAPSHOT_EXCLUSION_CAPACITY];
static struct mlc_root_range_s
	g_candidate_roots[MLC_SNAPSHOT_ROOT_CAPACITY];
static struct mlc_exclusion_range_s
	g_candidate_exclusion_inputs[MLC_SNAPSHOT_EXCLUSION_CAPACITY];
static struct mlc_root_input_s
	g_candidate_root_inputs[MLC_SNAPSHOT_ROOT_CAPACITY];
static struct mlc_loadable_mapping_input_s
	g_candidate_mapping_inputs[MLC_SNAPSHOT_MAPPING_CAPACITY];
static const void *g_candidate_sources[MLC_SNAPSHOT_ALLOC_CAPACITY];
static bool g_candidate_leak_flags[MLC_SNAPSHOT_ALLOC_CAPACITY];
static struct mlc_unified_root_s
	g_unified_root_inputs[MLC_SNAPSHOT_ROOT_CAPACITY];
static struct mlc_unified_group_s
	g_unified_groups[MLC_SNAPSHOT_ALLOC_CAPACITY];
static size_t g_candidate_snapshot_count;
static size_t g_candidate_root_count;
static size_t g_unified_group_count;
static size_t g_node_info_count;
static bool g_candidate_snapshot_active;
static struct mlc_budget_counters_s *g_active_budget;

static void mlc_clear_active_budget(void)
{
	g_active_budget = NULL;
	mlc_budget_bind(NULL);
}

struct mlc_snapshot_task_context_s {
	const struct up_mem_leak_capture_s *capture;
	struct mlc_lifecycle_s *lifecycle;
	struct tcb_s *current;
	size_t exclusion_count;
	size_t root_count;
	size_t mapping_count;
	int status;
	uint64_t registry_identity[MLC_SNAPSHOT_ROOT_CAPACITY];
	size_t registry_count;
};

static int mlc_snapshot_add_register_root(
		struct mlc_snapshot_task_context_s *context, uintptr_t begin,
		size_t size);

static int mlc_snapshot_registry_budget_end(
		struct mlc_snapshot_task_context_s *context)
{
	return mlc_lifecycle_budget_chunk_end(context->lifecycle,
		mlc_budget_clock_now());
}

static bool mlc_snapshot_stack_bounds(const struct tcb_s *tcb,
		uintptr_t *low, uintptr_t *high)
{
	uintptr_t adjusted = (uintptr_t)tcb->adj_stack_ptr;

#ifdef CONFIG_ARCH_ARMV7A_FAMILY
	if (adjusted < tcb->adj_stack_size) {
		return false;
	}
	*low = adjusted - tcb->adj_stack_size;
	*high = adjusted;
#else
	if (adjusted > UINTPTR_MAX - sizeof(uint32_t) ||
		adjusted + sizeof(uint32_t) < tcb->adj_stack_size) {
		return false;
	}
	*high = adjusted + sizeof(uint32_t);
	*low = *high - tcb->adj_stack_size;
#endif
	return *low < *high;
}

static void mlc_snapshot_add_task(struct tcb_s *tcb, void *arg)
{
	struct mlc_snapshot_task_context_s *context = arg;
	struct mlc_saved_task_roots_s saved;
	enum mlc_saved_context_mode_e mode = MLC_CONTEXT_BLOCKED;
	uint32_t expected_cpu = 0;
	uintptr_t stack_low;
	uintptr_t stack_high;
	uintptr_t live_start;
	int ret;

	if (context->status != MLC_CANDIDATE_SNAPSHOT_OK ||
		tcb == context->current) {
		return;
	}
	if (context->lifecycle == NULL || mlc_lifecycle_budget_chunk_begin(
		context->lifecycle, MLC_BUDGET_REGISTRY_ENUM, 1,
		mlc_budget_clock_now()) < 0) {
		context->status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		return;
	}
#ifdef CONFIG_SMP
	if (tcb->task_state == TSTATE_TASK_RUNNING) {
		mode = MLC_CONTEXT_REMOTE_PAUSED;
		expected_cpu = tcb->cpu;
	}
#endif
	if (!mlc_snapshot_stack_bounds(tcb, &stack_low, &stack_high) ||
		!mlc_validate_saved_task_roots(tcb, mode, expected_cpu, &saved)) {
		if (mlc_snapshot_registry_budget_end(context) < 0) {
			context->status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			return;
		}
		context->status = MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		return;
	}
	if (context->registry_count >= COUNT_OF(context->registry_identity)) {
		if (mlc_snapshot_registry_budget_end(context) < 0) {
			context->status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			return;
		}
		context->status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		return;
	}
	ret = mlc_budget_reserve_ownership_identity(&context->lifecycle->counters,
		MLC_BUDGET_REGISTRY_ENUM, MLC_BUDGET_REGISTRY_UNWIND,
		(uint64_t)(uintptr_t)tcb);
	if (ret < 0) {
		if (mlc_snapshot_registry_budget_end(context) < 0) {
			context->status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			return;
		}
		context->status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		return;
	}
	ret = mlc_budget_commit_ownership_identity(&context->lifecycle->counters,
		MLC_BUDGET_REGISTRY_ENUM, (uint64_t)(uintptr_t)tcb);
	if (ret < 0) {
		mlc_lifecycle_invoke_fatal(context->lifecycle, mlc_snapshot_fatal,
			context);
		return;
	}
	context->registry_identity[context->registry_count++] =
		(uint64_t)(uintptr_t)tcb;
	live_start = (uintptr_t)saved.stack_live_start;
	if (context->exclusion_count >= MLC_SNAPSHOT_EXCLUSION_CAPACITY ||
		context->root_count + 3 > MLC_SNAPSHOT_ROOT_CAPACITY) {
		if (mlc_snapshot_registry_budget_end(context) < 0) {
			context->status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			return;
		}
		context->status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		return;
	}
	g_candidate_exclusion_inputs[context->exclusion_count++] =
		(struct mlc_exclusion_range_s){{(uintptr_t)tcb, sizeof(*tcb)},
		MLC_EXCLUDE_ACTIVE_TCB};
	g_candidate_root_inputs[context->root_count++] =
		(struct mlc_root_input_s){{(uintptr_t)tcb, sizeof(*tcb)},
		MLC_ROOT_ACTIVE_TCB};
	g_candidate_exclusion_inputs[context->exclusion_count++] =
		(struct mlc_exclusion_range_s){{stack_low, stack_high - stack_low},
		MLC_EXCLUDE_FULL_STACK};
	g_candidate_root_inputs[context->root_count++] =
		(struct mlc_root_input_s){{live_start, stack_high - live_start},
		MLC_ROOT_TASK_STACK_LIVE};
	context->status = mlc_snapshot_add_register_root(context,
		(uintptr_t)saved.registers,
		saved.register_count * sizeof(*saved.registers));
	if (mlc_snapshot_registry_budget_end(context) < 0) {
		context->status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
}

static int mlc_snapshot_release_registry(
		struct mlc_snapshot_task_context_s *context)
{
	size_t index;

	for (index = context->registry_count; index > 0; index--) {
		uint64_t now = up_mem_leak_monotonic_usec();
		uint64_t identity = context->registry_identity[index - 1];

		if (mlc_lifecycle_budget_chunk_begin(context->lifecycle,
			MLC_BUDGET_REGISTRY_UNWIND, 1, now) < 0 ||
			mlc_budget_release_ownership_identity(
			&context->lifecycle->counters, MLC_BUDGET_REGISTRY_ENUM,
			MLC_BUDGET_REGISTRY_UNWIND, identity) < 0 ||
			mlc_lifecycle_budget_chunk_end(context->lifecycle,
			up_mem_leak_monotonic_usec()) < 0) {
			return -EUCLEAN;
		}
	}
	context->registry_count = 0;
	return 0;
}

static int mlc_snapshot_add_current_task(
		struct mlc_snapshot_task_context_s *context)
{
	uintptr_t stack_low;
	uintptr_t stack_high;
	uintptr_t live_start;

	if (context->current == NULL || context->capture == NULL ||
		!mlc_snapshot_stack_bounds(context->current, &stack_low, &stack_high)) {
		return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
	}
	live_start = (uintptr_t)context->capture->caller_boundary;
	if (live_start < stack_low || live_start >= stack_high ||
		context->exclusion_count + 2 > MLC_SNAPSHOT_EXCLUSION_CAPACITY ||
		context->root_count + 3 > MLC_SNAPSHOT_ROOT_CAPACITY) {
		return live_start < stack_low || live_start >= stack_high ?
			MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED :
			MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	g_candidate_exclusion_inputs[context->exclusion_count++] =
		(struct mlc_exclusion_range_s){{(uintptr_t)context->current,
		 sizeof(*context->current)}, MLC_EXCLUDE_ACTIVE_TCB};
	g_candidate_root_inputs[context->root_count++] =
		(struct mlc_root_input_s){{(uintptr_t)context->current,
		 sizeof(*context->current)}, MLC_ROOT_ACTIVE_TCB};
	g_candidate_exclusion_inputs[context->exclusion_count++] =
		(struct mlc_exclusion_range_s){{stack_low, stack_high - stack_low},
		MLC_EXCLUDE_FULL_STACK};
	g_candidate_root_inputs[context->root_count++] =
		(struct mlc_root_input_s){{live_start, stack_high - live_start},
		MLC_ROOT_TASK_STACK_LIVE};
	return mlc_snapshot_add_register_root(context,
		(uintptr_t)context->capture->callee_saved,
		sizeof(context->capture->callee_saved));
}

static int mlc_snapshot_add_register_root(
		struct mlc_snapshot_task_context_s *context, uintptr_t begin,
		size_t size)
{
	struct mlc_address_range_s range = { begin, size };
	size_t index;

	for (index = 0; index < context->root_count; index++) {
		if (mlc_candidate_range_contains(
			&g_candidate_root_inputs[index].range, &range)) {
			return MLC_CANDIDATE_SNAPSHOT_OK;
		}
		if (mlc_candidate_ranges_overlap(
			&g_candidate_root_inputs[index].range, &range)) {
			return MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
	}
	if (context->root_count >= MLC_SNAPSHOT_ROOT_CAPACITY) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	g_candidate_root_inputs[context->root_count++] =
		(struct mlc_root_input_s){range, MLC_ROOT_TASK_REGISTERS};
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

static int mlc_snapshot_add_range_exclusion(uintptr_t begin, size_t size,
		enum mlc_exclusion_kind_e kind, size_t *count)
{
	if (size == 0 || *count >= MLC_SNAPSHOT_EXCLUSION_CAPACITY) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	g_candidate_exclusion_inputs[(*count)++] =
		(struct mlc_exclusion_range_s){{begin, size}, kind};
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

static int mlc_snapshot_add_broad_root(uintptr_t begin, size_t size,
		size_t *count)
{
	if (size == 0 || *count >= MLC_SNAPSHOT_ROOT_CAPACITY) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	g_candidate_root_inputs[(*count)++] =
		(struct mlc_root_input_s){{begin, size}, MLC_ROOT_BROAD_STATIC};
	return MLC_CANDIDATE_SNAPSHOT_OK;
}

struct mlc_invocation_report_s;

static int mlc_exclude_report_buffers(
		const struct mlc_invocation_report_s *report, size_t *count);

static int mlc_collect_locked_candidates(struct mlc_lifecycle_s *lifecycle,
		struct mlc_domain_guard_s *guard,
		const struct mlc_invocation_report_s *report,
		const struct up_mem_leak_capture_s *capture)
{
	struct mlc_candidate_snapshot_request_s request;
	struct mlc_candidate_snapshot_workspace_s workspace;
	struct mlc_candidate_snapshot_result_s result;
	struct mlc_snapshot_task_context_s task_context;
	size_t index;
	int status;
	int end_ret;

	memset(g_candidate_exclusion_inputs, 0,
		sizeof(g_candidate_exclusion_inputs));
	memset(g_candidate_root_inputs, 0, sizeof(g_candidate_root_inputs));
	memset(g_candidate_mapping_inputs, 0,
		sizeof(g_candidate_mapping_inputs));
	memset(g_candidate_sources, 0, sizeof(g_candidate_sources));
	memset(g_candidate_leak_flags, 0, sizeof(g_candidate_leak_flags));
	memset(g_unified_root_inputs, 0, sizeof(g_unified_root_inputs));
	memset(g_unified_groups, 0, sizeof(g_unified_groups));
	memset(&task_context, 0, sizeof(task_context));
	task_context.capture = capture;
	task_context.lifecycle = lifecycle;
	task_context.current = current_task(this_cpu());
	task_context.status = MLC_CANDIDATE_SNAPSHOT_OK;
	status = mlc_snapshot_add_range_exclusion((uintptr_t)g_node_info,
		MAX_ALLOC_COUNT * sizeof(*g_node_info), MLC_EXCLUDE_CHECKER_CONTROL,
		&task_context.exclusion_count);
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion((uintptr_t)g_hash_table,
			HASH_SIZE * sizeof(*g_hash_table), MLC_EXCLUDE_CHECKER_CONTROL,
			&task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_exclude_report_buffers(report,
			&task_context.exclusion_count);
	}
	for (index = 0; status == MLC_CANDIDATE_SNAPSHOT_OK &&
		index < MEM_VAR_REGION_COUNT; index++) {
		if (mlc_lifecycle_budget_chunk_begin(lifecycle,
			MLC_BUDGET_ROOT_RANGE, 1, up_mem_leak_monotonic_usec()) < 0) {
			status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			break;
		}
		uintptr_t begin = (uintptr_t)variable_region_start_addr[index];
		uintptr_t end = (uintptr_t)variable_region_end_addr[index];

		if (end <= begin || end - begin > SIZE_MAX) {
			status = MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		} else {
			status = mlc_snapshot_add_broad_root(begin, end - begin,
				&task_context.root_count);
		}
		end_ret = mlc_lifecycle_budget_chunk_end(lifecycle,
			up_mem_leak_monotonic_usec());
		if (end_ret < 0) {
			status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
		}
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion(
			(uintptr_t)g_candidate_snapshot, sizeof(g_candidate_snapshot),
			MLC_EXCLUDE_CHECKER_CONTROL, &task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion(
			(uintptr_t)g_candidate_exclusions, sizeof(g_candidate_exclusions),
			MLC_EXCLUDE_CHECKER_CONTROL, &task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion(
			(uintptr_t)g_candidate_roots, sizeof(g_candidate_roots),
			MLC_EXCLUDE_CHECKER_CONTROL, &task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion(
			(uintptr_t)g_candidate_exclusion_inputs,
			sizeof(g_candidate_exclusion_inputs), MLC_EXCLUDE_CHECKER_CONTROL,
			&task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion(
			(uintptr_t)g_candidate_root_inputs, sizeof(g_candidate_root_inputs),
			MLC_EXCLUDE_CHECKER_CONTROL, &task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion(
			(uintptr_t)g_candidate_mapping_inputs,
			sizeof(g_candidate_mapping_inputs), MLC_EXCLUDE_CHECKER_CONTROL,
			&task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion(
			(uintptr_t)g_candidate_sources, sizeof(g_candidate_sources),
			MLC_EXCLUDE_CHECKER_CONTROL, &task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion(
			(uintptr_t)g_candidate_leak_flags, sizeof(g_candidate_leak_flags),
			MLC_EXCLUDE_CHECKER_CONTROL, &task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion(
			(uintptr_t)g_unified_root_inputs, sizeof(g_unified_root_inputs),
			MLC_EXCLUDE_CHECKER_CONTROL, &task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_range_exclusion(
			(uintptr_t)g_unified_groups, sizeof(g_unified_groups),
			MLC_EXCLUDE_CHECKER_CONTROL, &task_context.exclusion_count);
	}
	for (index = 0; status == MLC_CANDIDATE_SNAPSHOT_OK &&
		index < mlc_unified_control_range_count(); index++) {
		uintptr_t begin;
		size_t size;

		if (mlc_unified_control_range(index, &begin, &size) != MLC_CORE_OK) {
			status = MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
			break;
		}
		status = mlc_snapshot_add_range_exclusion(begin, size,
			MLC_EXCLUDE_CHECKER_CONTROL, &task_context.exclusion_count);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = mlc_snapshot_add_current_task(&task_context);
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		sched_foreach(mlc_snapshot_add_task, &task_context);
		status = task_context.status;
	}
#ifdef CONFIG_APP_BINARY_SEPARATION
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		for (index = 0; index < guard->pin_count; index++) {
			if (mlc_lifecycle_budget_chunk_begin(lifecycle,
				MLC_BUDGET_DOMAIN_PIN, 1, up_mem_leak_monotonic_usec()) < 0) {
				status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
				break;
			}
			const struct mm_loadable_domain_pin_s *pin = &guard->pins[index];
			size_t mapping_index;

			for (mapping_index = 0; mapping_index < pin->writable_count;
				mapping_index++) {
				if (mlc_lifecycle_budget_chunk_begin(lifecycle,
					MLC_BUDGET_ROOT_CONTAINER_ENUM, 1,
					up_mem_leak_monotonic_usec()) < 0) {
					status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
					break;
				}
				const struct mm_loadable_mapping_s *mapping =
					&pin->writable[mapping_index];
				uintptr_t begin = mapping->start;

				if (task_context.mapping_count >= MLC_SNAPSHOT_MAPPING_CAPACITY) {
					status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
					if (mlc_lifecycle_budget_chunk_end(lifecycle,
						mlc_budget_clock_now()) < 0) {
						status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
					}
					break;
				}
				g_candidate_mapping_inputs[task_context.mapping_count++] =
					(struct mlc_loadable_mapping_input_s){
					{begin, mapping->size},
					{mapping->container, mapping->container_size}};
				if (mlc_lifecycle_budget_chunk_end(lifecycle,
					mlc_budget_clock_now()) < 0) {
					status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
					break;
				}
			}
			if (mlc_lifecycle_budget_chunk_end(lifecycle,
				mlc_budget_clock_now()) < 0) {
				status = MLC_CANDIDATE_SNAPSHOT_CAPACITY;
			}
			if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
				break;
			}
		}
	}
	#endif
	if (status != MLC_CANDIDATE_SNAPSHOT_OK) {
		if (mlc_snapshot_release_registry(&task_context) < 0) {
			status = MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
		}
		return status;
	}
	memset(&request, 0, sizeof(request));
	memset(&workspace, 0, sizeof(workspace));
	memset(&result, 0, sizeof(result));
	request.heaps = guard->heaps;
	request.heap_count = guard->heap_count;
	request.exclusions = g_candidate_exclusion_inputs;
	request.exclusion_count = task_context.exclusion_count;
	request.roots = g_candidate_root_inputs;
	request.root_count = task_context.root_count;
	request.mappings = g_candidate_mapping_inputs;
	request.mapping_count = task_context.mapping_count;
	workspace.candidates = g_candidate_snapshot;
	workspace.candidate_capacity = MLC_SNAPSHOT_ALLOC_CAPACITY;
	workspace.exclusions = g_candidate_exclusions;
	workspace.exclusion_capacity = MLC_SNAPSHOT_EXCLUSION_CAPACITY;
	workspace.roots = g_candidate_roots;
	workspace.root_capacity = MLC_SNAPSHOT_ROOT_CAPACITY;
	status = mlc_candidate_snapshot_collect(&request, &workspace, &result);
	if (mlc_snapshot_release_registry(&task_context) < 0 &&
		status == MLC_CANDIDATE_SNAPSHOT_OK) {
		status = MLC_CANDIDATE_SNAPSHOT_DOMAIN_CHANGED;
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_OK) {
		g_candidate_snapshot_count = result.candidate_count;
		g_candidate_root_count = result.root_count;
		g_candidate_snapshot_active = true;
	}
	return status;
}

static enum mlc_incomplete_reason_e mlc_candidate_failure_reason(int status)
{
	if (status == MLC_CANDIDATE_SNAPSHOT_CAPACITY) {
		return MLC_INCOMPLETE_BUDGET;
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_CONTENTION) {
		return MLC_INCOMPLETE_CONTENTION;
	}
	if (status == MLC_CANDIDATE_SNAPSHOT_HEAP_CORRUPT) {
		return MLC_INCOMPLETE_HEAP_CORRUPT;
	}
	return MLC_INCOMPLETE_DOMAIN_CHANGED;
}

static void mlc_clear_locked_candidates(void)
{
	g_candidate_snapshot_active = false;
	g_candidate_snapshot_count = 0;
	g_candidate_root_count = 0;
	g_unified_group_count = 0;
	memset(g_candidate_snapshot, 0, sizeof(g_candidate_snapshot));
	memset(g_candidate_exclusions, 0, sizeof(g_candidate_exclusions));
	memset(g_candidate_roots, 0, sizeof(g_candidate_roots));
	memset(g_candidate_sources, 0, sizeof(g_candidate_sources));
	memset(g_candidate_leak_flags, 0, sizeof(g_candidate_leak_flags));
	memset(g_unified_root_inputs, 0, sizeof(g_unified_root_inputs));
	memset(g_unified_groups, 0, sizeof(g_unified_groups));
	mlc_unified_workspace_reset();
}

enum mlc_node_state_e {
	MLC_NODE_UNUSED,
	MLC_NODE_USED,
	MLC_NODE_LEAK,
	MLC_NODE_AMBIGUOUS,
};

enum mlc_report_row_type_e {
	MLC_REPORT_LEAK,
	MLC_REPORT_BROKEN,
	MLC_REPORT_AMBIGUOUS,
	MLC_REPORT_DETAIL
};

struct mlc_report_row_s {
	enum mlc_report_row_type_e type;
	void *address;
	mmsize_t size;
	size_t requested_size;
	uint32_t owner_address;
	pid_t pid;
	size_t dump_size;
	unsigned char dump[MEM_DUMP_MAX_BYTES];
	size_t scc_id;
	uint8_t provenance;
	bool direct;
};

struct mlc_heap_report_s {
	char name[MLC_REPORT_NAME_MAX];
	size_t first_row;
	size_t row_count;
	int leak_count;
	int ambiguous_count;
	int broken_count;
	uintptr_t text_start;
	size_t text_size;
};

struct mlc_invocation_report_s {
	struct mlc_report_row_s *rows;
	struct mlc_heap_report_s *heaps;
	size_t row_capacity;
	size_t heap_count;
	size_t heap_capacity;
};

static int mlc_exclude_report_buffers(
		const struct mlc_invocation_report_s *report, size_t *count)
{
	size_t row_bytes;
	size_t heap_bytes;

	if (report == NULL || report->rows == NULL || report->heaps == NULL ||
		report->row_capacity == 0 || report->heap_capacity == 0 ||
		report->row_capacity > SIZE_MAX / sizeof(*report->rows) ||
		report->heap_capacity > SIZE_MAX / sizeof(*report->heaps)) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	row_bytes = report->row_capacity * sizeof(*report->rows);
	heap_bytes = report->heap_capacity * sizeof(*report->heaps);
	if (mlc_snapshot_add_range_exclusion((uintptr_t)report->rows, row_bytes,
			MLC_EXCLUDE_CHECKER_CONTROL, count) !=
		MLC_CANDIDATE_SNAPSHOT_OK) {
		return MLC_CANDIDATE_SNAPSHOT_CAPACITY;
	}
	return mlc_snapshot_add_range_exclusion((uintptr_t)report->heaps,
		heap_bytes, MLC_EXCLUDE_CHECKER_CONTROL, count);
}

#ifdef CONFIG_TC_KERNEL_MEM_LEAK_CHECKER
static void (*g_test_observer)(void *allocation, bool leak);
static int g_test_node_count;

void mem_leak_checker_set_test_observer(void (*observer)(void *allocation, bool leak))
{
	g_test_observer = observer;
}

static void notify_test_observer(void)
{
	int index;

	if (g_test_observer == NULL) {
		return;
	}
	for (index = 0; index < g_test_node_count; index++) {
		g_test_observer(
			(void *)((char *)g_node_info[index].node + SIZEOF_MM_ALLOCNODE),
			g_node_info[index].state == MLC_NODE_LEAK);
	}
}
#endif

static void print_already_running(void)
{
	printf("mem_leak_checker is already running.\n");
}

static void cleanup_workspace(void *arg)
{
	void **allocation = (void **)arg;

	free(*allocation);
	*allocation = NULL;
}

static int report_init(struct mlc_lifecycle_s *lifecycle,
		struct mlc_invocation_report_s *report, size_t heap_capacity)
{
	size_t row_capacity;

	memset(report, 0, sizeof(*report));
	if (heap_capacity == 0 || heap_capacity > SIZE_MAX / MAX_ALLOC_COUNT) {
		return ERROR;
	}
	row_capacity = heap_capacity * MAX_ALLOC_COUNT;
	if (row_capacity > SIZE_MAX / MLC_REPORT_ROW_CAPACITY_FACTOR) {
		return ERROR;
	}
	row_capacity *= MLC_REPORT_ROW_CAPACITY_FACTOR;
	if (row_capacity > SIZE_MAX / sizeof(*report->rows) ||
		heap_capacity > SIZE_MAX / sizeof(*report->heaps)) {
		return ERROR;
	}

	report->rows = malloc(row_capacity * sizeof(*report->rows));
	if (report->rows == NULL) {
		return ERROR;
	}
	memset(report->rows, 0, row_capacity * sizeof(*report->rows));
	if (mlc_lifecycle_push(lifecycle, MLC_PHASE_WORKSPACE,
		MLC_RESOURCE_WORKSPACE, cleanup_workspace, &report->rows) < 0) {
		cleanup_workspace(&report->rows);
		return ERROR;
	}
	report->heaps = malloc(heap_capacity * sizeof(*report->heaps));
	if (report->heaps == NULL) {
		return ERROR;
	}
	if (mlc_lifecycle_push(lifecycle, MLC_PHASE_WORKSPACE,
		MLC_RESOURCE_WORKSPACE, cleanup_workspace, &report->heaps) < 0) {
		cleanup_workspace(&report->heaps);
		return ERROR;
	}
	memset(report->heaps, 0, heap_capacity * sizeof(*report->heaps));
	report->row_capacity = row_capacity;
	report->heap_capacity = heap_capacity;
	return mlc_lifecycle_bind_report(lifecycle, report->rows, row_capacity,
		sizeof(*report->rows)) < 0 ? ERROR : OK;
}

static int hash_init(struct mlc_lifecycle_s *lifecycle)
{
	int index;

	g_hash_table = (struct alloc_node_info_s **)malloc(sizeof(struct alloc_node_info_s *) * HASH_SIZE);
	if (!g_hash_table) {
		return ERROR;
	}
	if (mlc_lifecycle_push(lifecycle, MLC_PHASE_WORKSPACE,
			MLC_RESOURCE_WORKSPACE, cleanup_workspace,
			(void *)&g_hash_table) < 0) {
		cleanup_workspace((void *)&g_hash_table);
		return ERROR;
	}

	g_node_info = (struct alloc_node_info_s*)malloc(
		sizeof(struct alloc_node_info_s) * MLC_NODE_INFO_CAPACITY);
	if (!g_node_info) {
		return ERROR;
	}
	if (mlc_lifecycle_push(lifecycle, MLC_PHASE_WORKSPACE,
			MLC_RESOURCE_WORKSPACE, cleanup_workspace,
			(void *)&g_node_info) < 0) {
		cleanup_workspace((void *)&g_node_info);
		return ERROR;
	}

	for (index = 0; index < HASH_SIZE; ++index) {
		g_hash_table[index] = NULL;
	}
#ifdef CONFIG_TC_KERNEL_MEM_LEAK_CHECKER
	g_test_node_count = 0;
#endif

	return OK;
}

static void hash_reset(void)
{
	memset(g_hash_table, 0, sizeof(*g_hash_table) * HASH_SIZE);
	memset(g_node_info, 0, sizeof(*g_node_info) * MLC_NODE_INFO_CAPACITY);
	g_node_info_count = 0;
#ifdef CONFIG_TC_KERNEL_MEM_LEAK_CHECKER
	g_test_node_count = 0;
#endif
}

static void add_hash(int index)
{
	size_t key;
	struct alloc_node_info_s *cur;

	key = (uintptr_t)g_node_info[index].node % HASH_SIZE;
	if (g_hash_table[key] == NULL) {
		g_hash_table[key] = &g_node_info[index];
		return;
	}

	cur = g_hash_table[key];
	while (cur->next) {
		cur = cur->next;
	}
	cur->next = &g_node_info[index];
}

static int populate_snapshot_hash(void)
{
	size_t candidate_index;

	if (!g_candidate_snapshot_active) {
		return OK;
	}
	for (candidate_index = 0;
		candidate_index < g_candidate_snapshot_count; candidate_index++) {
		struct alloc_node_info_s *info;
		uintptr_t node_address = g_candidate_snapshot[candidate_index].payload_begin -
			(uintptr_t)SIZEOF_MM_ALLOCNODE;

		if (g_node_info_count >= MLC_NODE_INFO_CAPACITY) {
			return ERROR;
		}
		info = &g_node_info[g_node_info_count];
		info->node = (volatile struct mm_allocnode_s *)node_address;
		info->next = NULL;
		info->state = MLC_NODE_LEAK;
		add_hash((int)g_node_info_count);
		g_node_info_count++;
		g_candidate_sources[candidate_index] =
			(const void *)g_candidate_snapshot[candidate_index].payload_begin;
	}
#ifdef CONFIG_TC_KERNEL_MEM_LEAK_CHECKER
	g_test_node_count = (int)g_node_info_count;
#endif
	return OK;
}

static int analyze_unified_snapshot(void)
{
	size_t index;
	int result;

	if (!g_candidate_snapshot_active ||
		g_candidate_snapshot_count > MLC_SNAPSHOT_ALLOC_CAPACITY ||
		g_candidate_root_count > MLC_SNAPSHOT_ROOT_CAPACITY) {
		return ERROR;
	}
	for (index = 0; index < g_candidate_root_count; index++) {
		g_unified_root_inputs[index] = (struct mlc_unified_root_s){
			(const void *)g_candidate_roots[index].range.begin,
			g_candidate_roots[index].range.size,
			g_candidate_roots[index].range.begin};
	}
	g_unified_group_count = 0;
	result = mlc_unified_analyze(g_candidate_snapshot,
		g_candidate_snapshot_count, g_candidate_sources,
		g_unified_root_inputs, g_candidate_root_count,
		g_candidate_leak_flags, g_unified_groups,
		MLC_SNAPSHOT_ALLOC_CAPACITY, &g_unified_group_count);
	if (result != MLC_CORE_OK) {
		return ERROR;
	}
	for (index = 0; index < g_node_info_count; index++) {
		if (g_candidate_leak_flags[index]) {
			g_node_info[index].state = MLC_NODE_LEAK;
		} else if (mlc_unified_reachability(index) == MLC_REACH_AMBIGUOUS) {
			g_node_info[index].state = MLC_NODE_AMBIGUOUS;
		} else {
			g_node_info[index].state = MLC_NODE_USED;
		}
	}
	return OK;
}

static struct alloc_node_info_s *find_hash_entry(uintptr_t value)
{
	size_t key = value % HASH_SIZE;
	struct alloc_node_info_s *cur = g_hash_table[key];

	while (cur != NULL) {
		if ((uintptr_t)cur->node == value) {
			return cur;
		}
		if (cur->next == NULL) {
			return NULL;
		}
		cur = cur->next;
	}
	return NULL;
}

static bool search_hash(uintptr_t value)
{
	struct alloc_node_info_s *entry = find_hash_entry(value);

	if (entry == NULL || entry->state == MLC_NODE_USED) {
		return false;
	}
	entry->state = MLC_NODE_USED;
	return true;
}

static int get_node_cnt(struct mm_heap_s *heap)
{
	volatile struct mm_allocnode_s *node;
	mmsize_t node_size;
	node_size = SIZEOF_MM_ALLOCNODE;

	int ret = 0;

#if CONFIG_KMM_REGIONS > 1
	int region;
#else
#define region 0
#endif

	/* Visit each region */

#if CONFIG_KMM_REGIONS > 1
	for (region = 0; region < heap->mm_nregions; region++)
#endif
	{
		node_size = SIZEOF_MM_ALLOCNODE;
		for (node = heap->mm_heapstart[region]; node < heap->mm_heapend[region]; node = (struct mm_allocnode_s *)((char *)node + node->size)) {
			ASSERT(node->size);
			/* Ignore the heap start checking, because there is a guard node in heap start */
			if (node == heap->mm_heapstart[region]) {
				continue;
			}
			/* Check broken link */
			if (node_size != MM_PREV_NODE_SIZE(node)) {
				continue;
			}
			node_size = node->size;
			/* Check if the node corresponds to an allocated memory chunk */
			if ((node->preceding & MM_ALLOC_BIT) != 0) {
				ret++;
			}
		}
	}

	return ret;
}

static void fill_hash_table(struct mm_heap_s *heap, int *leak_cnt, int *broken_cnt)
{
	volatile struct mm_allocnode_s *node;
	mmsize_t node_size;
	node_size = SIZEOF_MM_ALLOCNODE;

#if CONFIG_KMM_REGIONS > 1
	int region;
#else
#define region 0
#endif

	/* Visit each region */

#if CONFIG_KMM_REGIONS > 1
	for (region = 0; region < heap->mm_nregions; region++)
#endif
	{
		node_size = SIZEOF_MM_ALLOCNODE;
		for (node = heap->mm_heapstart[region]; node < heap->mm_heapend[region]; node = (struct mm_allocnode_s *)((char *)node + node->size)) {
			ASSERT(node->size);
			/* Ignore the heap start checking, because there is a guard node in heap start */
			if (node == heap->mm_heapstart[region]) {
				continue;
			}

			/* Check broken link */
			if (node_size != MM_PREV_NODE_SIZE(node)) {
				(*broken_cnt)++;
				continue;
			}
			node_size = node->size;
			if ((unsigned long)node + (unsigned long)SIZEOF_MM_ALLOCNODE == (unsigned long)g_node_info ||
					(unsigned long)node + (unsigned long)SIZEOF_MM_ALLOCNODE == (unsigned long)g_hash_table) {
				continue;
			}
			/* Check if the node corresponds to an allocated memory chunk */
			if ((node->preceding & MM_ALLOC_BIT) != 0) {
				if (g_candidate_snapshot_active) {
					struct alloc_node_info_s *info = find_hash_entry(
						(uintptr_t)node);

					if (info == NULL) {
						continue;
					}
					(*leak_cnt)++;
					continue;
				}
				g_node_info[*leak_cnt].node = node;
				g_node_info[*leak_cnt].next = NULL;
				g_node_info[*leak_cnt].state = MLC_NODE_LEAK;
				add_hash(*leak_cnt);
				(*leak_cnt)++;
#ifdef CONFIG_TC_KERNEL_MEM_LEAK_CHECKER
				g_test_node_count = *leak_cnt;
#endif
			}
		}
	}
}

static void search_addr(void *start_addr, void *end_addr, int *leak_cnt)
{
	unsigned char *cursor = start_addr;
	unsigned char *end = end_addr;
	uintptr_t value;

	while ((size_t)(end - cursor) >= sizeof(value)) {
		memcpy(&value, cursor, sizeof(value));
		if (search_hash(value - (uintptr_t)SIZEOF_MM_ALLOCNODE)) {
			(*leak_cnt)--;
		}
		cursor++;
	}
}

struct mlc_task_root_scan_s {
	struct tcb_s *current;
	int *leak_cnt;
	bool valid;
};

static void scan_saved_task_roots(struct tcb_s *tcb, void *arg)
{
	struct mlc_task_root_scan_s *scan = arg;
	struct mlc_saved_task_roots_s roots;

	if (!scan->valid || tcb == scan->current) {
		return;
	}
	if (!mlc_validate_saved_task_roots(tcb, MLC_CONTEXT_BLOCKED, 0, &roots)) {
		scan->valid = false;
		return;
	}

	search_addr((void *)roots.registers,
		(void *)(roots.registers + roots.register_count), scan->leak_cnt);
	search_addr((void *)roots.stack_live_start, (void *)roots.stack_high,
		scan->leak_cnt);
}

static int scan_task_roots(const struct up_mem_leak_capture_s *capture,
		int *leak_cnt)
{
	struct mlc_task_root_scan_s scan;
	uintptr_t stack_high;
	int cpu;

	cpu = this_cpu();
	scan.current = current_task(cpu);
	scan.leak_cnt = leak_cnt;
	scan.valid = scan.current != NULL &&
		capture->cpu == (uint32_t)cpu &&
		capture->tcb == (uint32_t)(uintptr_t)scan.current;
	if (scan.valid) {
		stack_high = (uintptr_t)scan.current->adj_stack_ptr;
#ifndef CONFIG_ARCH_ARMV7A_FAMILY
		stack_high += sizeof(uint32_t);
#endif
		search_addr((void *)capture->callee_saved,
			(void *)(capture->callee_saved + 8), leak_cnt);
		search_addr((void *)(uintptr_t)capture->caller_boundary,
			(void *)stack_high, leak_cnt);
		sched_foreach(scan_saved_task_roots, &scan);
	}
	return scan.valid ? OK : ERROR;
}

static bool mlc_payload_in_heap(const struct mm_heap_s *heap,
		uintptr_t payload)
{
	size_t region_index;

#if CONFIG_KMM_REGIONS > 1
	for (region_index = 0; region_index < (size_t)heap->mm_nregions;
		region_index++) {
#else
	region_index = 0;
	{
#endif
		uintptr_t begin = (uintptr_t)heap->mm_heapstart[region_index];
		uintptr_t end = (uintptr_t)heap->mm_heapend[region_index];

		if (begin <= UINTPTR_MAX - SIZEOF_MM_ALLOCNODE &&
			payload >= begin + SIZEOF_MM_ALLOCNODE && payload < end) {
			return true;
		}
	}
	return false;
}

static int mlc_analysis_budget_chunk(enum mlc_budget_counter_e counter,
		size_t operations)
{
	uint64_t now;

	if (g_active_budget == NULL) {
		return -EPERM;
	}
	now = mlc_budget_clock_now();
	if (mlc_budget_chunk_begin(g_active_budget, counter, operations, now) < 0) {
		return -E2BIG;
	}
	return mlc_budget_chunk_end(g_active_budget, mlc_budget_clock_now());
}

static int heap_check(struct mm_heap_s *heap, int checker_pid, int *leak_cnt)
{
	void *leak_chk;
	struct mm_allocnode_s *visit_node;
	size_t candidate_index;
	void *exclude_top;
	void *exclude_bottom;

	struct tcb_s *ctcb = sched_gettcb(checker_pid);
	ASSERT(ctcb != NULL);
	if (g_candidate_snapshot_active) {
		(void)checker_pid;
		*leak_cnt = 0;
		for (candidate_index = 0; candidate_index < g_node_info_count;
			candidate_index++) {
			if (mlc_analysis_budget_chunk(MLC_BUDGET_EDGE_RESCAN, 1) < 0 ||
				mlc_analysis_budget_chunk(MLC_BUDGET_FREE_NODE, 1) < 0) {
				return ERROR;
			}
			if (g_node_info[candidate_index].node != NULL &&
				g_node_info[candidate_index].state == MLC_NODE_LEAK &&
				mlc_payload_in_heap(heap,
					(uintptr_t)g_node_info[candidate_index].node +
					SIZEOF_MM_ALLOCNODE)) {
				(*leak_cnt)++;
			}
		}
		return OK;
	}
	exclude_top = ctcb->adj_stack_ptr;
	exclude_bottom = ctcb->adj_stack_ptr - ctcb->adj_stack_size;

#if CONFIG_KMM_REGIONS > 1
	int region;
#else
#define region 0
#endif

	/* Visit each region */

#if CONFIG_KMM_REGIONS > 1
	for (region = 0; region < heap->mm_nregions; region++)
#endif
	{
		for (visit_node = heap->mm_heapstart[region]; visit_node < heap->mm_heapend[region]; visit_node = (struct mm_allocnode_s *)((char *)visit_node + visit_node->size)) {
			if ((visit_node->preceding & MM_ALLOC_BIT) != 0) {
				if ((void *)((char *)visit_node + SIZEOF_MM_ALLOCNODE) == (void *)g_node_info) {
					continue;
				}
				for (leak_chk = (void *)visit_node; leak_chk < (void *)(((char *)visit_node) + visit_node->size); leak_chk++) {
					if ((leak_chk >= exclude_bottom && leak_chk <= exclude_top)) {
						continue;
					}
					if (search_hash(*(uintptr_t volatile *)leak_chk -
						(uintptr_t)SIZEOF_MM_ALLOCNODE)) {
						(*leak_cnt)--;
					}
				}
			}
		}
	}
	return OK;
}

static struct mm_heap_s * init_mem_leak_checker(int checker_pid, char *bin_name);

static int ram_check(struct mm_heap_s *heap, int checker_pid,
		struct mlc_domain_guard_s *guard,
#ifdef CONFIG_APP_BINARY_SEPARATION
		const struct mm_loadable_domain_pin_s *domain,
#endif
		int *leak_cnt, const struct up_mem_leak_capture_s *capture)
{
#ifndef CONFIG_APP_BINARY_SEPARATION
	(void)guard;
#endif
#ifdef CONFIG_APP_BINARY_SEPARATION
	struct mm_heap_s *kheap;
	size_t domain_index;
#endif
	if (g_candidate_snapshot_active) {
		return heap_check(heap, checker_pid, leak_cnt);
	} else {
		int mem_region_idx;

		for (mem_region_idx = 0; mem_region_idx < MEM_VAR_REGION_COUNT;
			mem_region_idx++) {
			search_addr(variable_region_start_addr[mem_region_idx],
				variable_region_end_addr[mem_region_idx], leak_cnt);
		}
	}

	if (!g_candidate_snapshot_active && scan_task_roots(capture, leak_cnt) != OK) {
		return ERROR;
	}

#ifdef CONFIG_APP_BINARY_SEPARATION
	if (domain == NULL || g_candidate_snapshot_active) {
		/* do nothing */
	} else {
		for (domain_index = 0; domain_index < guard->pin_count;
			domain_index++) {
			const struct mm_loadable_domain_pin_s *pin =
				&guard->pins[domain_index];
			size_t mapping_index;

			if (pin != domain && pin->heap != NULL) {
				continue;
			}
			for (mapping_index = 0; mapping_index < pin->writable_count;
				mapping_index++) {
				uintptr_t start = pin->writable[mapping_index].start;

				search_addr((void *)start,
					(void *)(start + pin->writable[mapping_index].size),
					leak_cnt);
			}
		}
		/* search the kernel heap first */
		kheap = kmm_get_baseheap();
		if (heap_check(kheap, checker_pid, leak_cnt) != OK) {
			return ERROR;
		}
	}
#endif

	/* Visit heap region */
	if (heap_check(heap, checker_pid, leak_cnt) != OK) {
		return ERROR;
	}
	return OK;
}

static void print_mem_hex_dump(const unsigned char *dump, size_t dump_size)
{
	size_t i;

	printf("[DATA] ");
	for (i = 0; i < dump_size; i++) {
		printf("%02x ", dump[i]);
		if ((i + 1) % 16 == 0 && (i + 1) < dump_size) {
			printf("\n       ");
		}
	}
	printf("\n");
}

static int capture_info(struct mlc_lifecycle_s *lifecycle,
		struct mlc_invocation_report_s *report, struct mm_heap_s *heap,
		const char *name, int leak_cnt, int broken_cnt)
{
	volatile struct mm_allocnode_s *node;
	mmsize_t node_size;
	struct mlc_heap_report_s *heap_report;
	size_t name_length;
	size_t index;
	int end_ret;

	if (report->heap_count >= report->heap_capacity) {
		return ERROR;
	}
	heap_report = &report->heaps[report->heap_count];
	name_length = strnlen(name, sizeof(heap_report->name));
	if (name_length >= sizeof(heap_report->name)) {
		return ERROR;
	}
	memcpy(heap_report->name, name, name_length + 1);
	heap_report->first_row = lifecycle->report.count;
	heap_report->leak_count = leak_cnt;
	heap_report->ambiguous_count = 0;
	heap_report->broken_count = broken_cnt;

	if (leak_cnt > 0 || broken_cnt > 0 || g_candidate_snapshot_active) {
#if CONFIG_KMM_REGIONS > 1
		int region;
#else
#define region 0
#endif

		/* Visit each region */

#if CONFIG_KMM_REGIONS > 1
		for (region = 0; region < heap->mm_nregions; region++)
#endif
		{
			node_size = SIZEOF_MM_ALLOCNODE;
			for (node = heap->mm_heapstart[region]; node <  heap->mm_heapend[region]; node = (struct mm_allocnode_s *)((char *)node + node->size)) {
				struct mlc_report_row_s row;
				struct alloc_node_info_s *info = NULL;
				bool broken = false;
				size_t key;

				ASSERT(node->size);
				if (mlc_lifecycle_budget_chunk_begin(lifecycle,
					MLC_BUDGET_REPORT_ROW, 1,
					up_mem_leak_monotonic_usec()) < 0) {
					return ERROR;
				}
				if (node == heap->mm_heapstart[region]) {
					if (mlc_lifecycle_budget_chunk_end(lifecycle,
						mlc_budget_clock_now()) < 0) {
						return ERROR;
					}
					continue;
				}
				if (node_size != MM_PREV_NODE_SIZE(node)) {
					broken = true;
				} else {
					node_size = node->size;
					key = (uintptr_t)node % HASH_SIZE;
					for (info = g_hash_table[key]; info; info = info->next) {
						if (info->node == node) {
							break;
						}
					}
				}
				if (info && info->state == MLC_NODE_LEAK) {
					void *payload = (void *)((char *)node + SIZEOF_MM_ALLOCNODE);
					size_t capacity = node->size - SIZEOF_MM_ALLOCNODE;
					size_t requested_size = capacity;

#ifdef CONFIG_DEBUG_MM_HEAPINFO
					if (!mm_allocnode_get_requested_size(
						(const struct mm_allocnode_s *)node, &requested_size)) {
						return ERROR;
					}
#endif
					if (requested_size > capacity) {
						return ERROR;
					}

					memset(&row, 0, sizeof(row));
					row.type = MLC_REPORT_LEAK;
					row.address = payload;
					row.size = capacity;
					row.owner_address = (uint32_t)node->alloc_call_addr;
					row.pid = node->pid < 0 ? -node->pid : node->pid;
					row.requested_size = requested_size;
					row.dump_size = requested_size < MEM_DUMP_MAX_BYTES ?
						requested_size : MEM_DUMP_MAX_BYTES;
					if (mlc_lifecycle_budget_chunk_begin(lifecycle,
						MLC_BUDGET_COPY_BYTES, 1,
						up_mem_leak_monotonic_usec()) < 0) {
						if (mlc_lifecycle_budget_chunk_end(lifecycle,
							mlc_budget_clock_now()) < 0) {
							return ERROR;
						}
						return ERROR;
					}
					memcpy(row.dump, payload, row.dump_size);
					{
						struct mlc_report_record_s record = {
							.type = MLC_REPORT_RECORD_DEFINITE,
							.address = (uintptr_t)row.address,
							.capacity = row.size,
							.requested_size = row.requested_size,
							.owner = row.owner_address,
							.pid = row.pid,
							.dump = row.dump,
							.dump_size = row.dump_size,
							.scc_id = row.scc_id,
							.provenance = row.provenance
						};

						if (mlc_report_record_validate(&record) < 0) {
							return ERROR;
						}
					}
					end_ret = mlc_lifecycle_budget_chunk_end(lifecycle,
						mlc_budget_clock_now());
					if (end_ret < 0) {
						if (mlc_lifecycle_budget_chunk_end(lifecycle,
							mlc_budget_clock_now()) < 0) {
							return ERROR;
						}
						return ERROR;
					}
					if (mlc_lifecycle_store_provisional(lifecycle, &row) < 0) {
						if (mlc_lifecycle_budget_chunk_end(lifecycle,
							mlc_budget_clock_now()) < 0) {
							return ERROR;
						}
						return ERROR;
					}
				} else if (info && info->state == MLC_NODE_AMBIGUOUS) {
					void *payload = (void *)((char *)node + SIZEOF_MM_ALLOCNODE);
					size_t capacity = node->size - SIZEOF_MM_ALLOCNODE;
					size_t requested_size = capacity;

#ifdef CONFIG_DEBUG_MM_HEAPINFO
					if (!mm_allocnode_get_requested_size(
						(const struct mm_allocnode_s *)node, &requested_size)) {
						return ERROR;
					}
#endif
					if (requested_size > capacity) {
						return ERROR;
					}
					memset(&row, 0, sizeof(row));
					row.type = MLC_REPORT_AMBIGUOUS;
					row.address = payload;
					row.size = capacity;
					row.requested_size = requested_size;
					row.owner_address = (uint32_t)node->alloc_call_addr;
					row.pid = node->pid < 0 ? -node->pid : node->pid;
					{
						struct mlc_report_record_s record = {
							.type = MLC_REPORT_RECORD_AMBIGUOUS,
							.address = (uintptr_t)row.address,
							.capacity = row.size,
							.requested_size = row.requested_size,
							.owner = row.owner_address,
							.pid = row.pid,
							.dump = NULL,
							.dump_size = 0,
							.scc_id = row.scc_id,
							.provenance = row.provenance
						};

						if (mlc_report_record_validate(&record) < 0) {
							return ERROR;
						}
					}
					if (mlc_lifecycle_store_provisional(lifecycle, &row) < 0) {
						return ERROR;
					}
					heap_report->ambiguous_count++;
				} else if (broken) {
					memset(&row, 0, sizeof(row));
					row.type = MLC_REPORT_BROKEN;
					row.address = (void *)node;
					if (mlc_lifecycle_store_provisional(lifecycle, &row) < 0) {
						if (mlc_lifecycle_budget_chunk_end(lifecycle,
							mlc_budget_clock_now()) < 0) {
							return ERROR;
						}
						return ERROR;
					}
				}
				if (mlc_lifecycle_budget_chunk_end(lifecycle,
					mlc_budget_clock_now()) < 0) {
					return ERROR;
				}
			}
		}

		}

	for (index = 0; index < g_unified_group_count; index++) {
		const struct mlc_unified_group_s *group = &g_unified_groups[index];
		struct mlc_report_row_s row;
		struct mlc_report_record_s record;

		if (!mlc_payload_in_heap(heap, group->representative)) {
			continue;
		}
		if (mlc_lifecycle_budget_chunk_begin(lifecycle, MLC_BUDGET_REPORT_ROW,
			1, up_mem_leak_monotonic_usec()) < 0) {
			return ERROR;
		}
		memset(&row, 0, sizeof(row));
		row.type = MLC_REPORT_DETAIL;
		row.address = (void *)group->representative;
		row.size = group->member_count;
		row.scc_id = index;
		row.provenance = group->strongest_provenance;
		row.direct = group->direct;
		record = (struct mlc_report_record_s){
			.type = MLC_REPORT_RECORD_DETAIL,
			.address = (uintptr_t)row.address,
			.capacity = row.size,
			.requested_size = 0,
			.owner = 0,
			.pid = 0,
			.dump = NULL,
			.dump_size = 0,
			.scc_id = row.scc_id,
			.provenance = row.provenance
		};
		if (mlc_report_record_validate(&record) < 0 ||
			mlc_lifecycle_budget_chunk_end(lifecycle,
				mlc_budget_clock_now()) < 0 ||
			mlc_lifecycle_store_provisional(lifecycle, &row) < 0) {
			return ERROR;
		}
	}

	heap_report->row_count = lifecycle->report.count - heap_report->first_row;
	report->heap_count++;
	return OK;
}

static void print_heap_report(const struct mlc_invocation_report_s *report,
		const struct mlc_heap_report_s *heap_report)
{
	size_t index;

	if (heap_report->leak_count > 0 || heap_report->ambiguous_count > 0 ||
		heap_report->broken_count > 0) {
		printf("Type   |    Addr    | Size(byte) |    Owner   | PID \n");
		printf("---------------------------------------------------\n");
		for (index = heap_report->first_row;
			index < heap_report->first_row + heap_report->row_count; index++) {
			const struct mlc_report_row_s *row = &report->rows[index];

			if (row->type == MLC_REPORT_LEAK) {
				printf("LEAK   | %10p |  %8d  | %10p | %d\n", row->address,
					row->size, row->owner_address, row->pid);
				print_mem_hex_dump(row->dump, row->dump_size);
			} else if (row->type == MLC_REPORT_BROKEN) {
				printf("BROKEN | %p\n", row->address);
			}
		}
		if (heap_report->leak_count > 0 || heap_report->broken_count > 0) {
			printf("*** %d LEAKS, %d BROKENS.\n", heap_report->leak_count,
				heap_report->broken_count);
		}
	} else {
		printf("*** NO MEMORY LEAK.\n");
	}
}

static const char *report_provenance_name(uint8_t provenance)
{
	if ((provenance & MLC_PROVENANCE_UNALIGNED) != 0) {
		return "UNALIGNED";
	}
	if ((provenance & MLC_PROVENANCE_INTERIOR) != 0) {
		return "INTERIOR";
	}
	if ((provenance & MLC_PROVENANCE_ALIGNED_EXACT) != 0) {
		return "ALIGNED_EXACT";
	}
	return "UNKNOWN";
}

static void print_extended_report_rows(
		const struct mlc_invocation_report_s *report,
		const struct mlc_heap_report_s *heap_report)
{
	size_t index;

	for (index = heap_report->first_row;
		index < heap_report->first_row + heap_report->row_count; index++) {
		const struct mlc_report_row_s *row = &report->rows[index];

		if (row->type == MLC_REPORT_AMBIGUOUS) {
			printf("AMBIGUOUS | %10p |  %8d  | %10p | %d\n",
				row->address, row->size, row->owner_address, row->pid);
		} else if (row->type == MLC_REPORT_DETAIL) {
			printf("DETAIL | SCC=%u class=%s provenance=%s representative=%p members=%u\n",
				(unsigned int)row->scc_id,
				row->direct ? "direct" : "indirect",
				report_provenance_name(row->provenance), row->address,
				(unsigned int)row->size);
		}
	}
	if (heap_report->leak_count == 0 && heap_report->broken_count == 0 &&
		heap_report->ambiguous_count > 0) {
		printf("*** NO DEFINITE MEMORY LEAK; %d RETAINED-AMBIGUOUS.\n",
			heap_report->ambiguous_count);
	}
}

static void print_incomplete(const struct mlc_lifecycle_s *lifecycle)
{
	const struct mlc_post_release_record_s *record =
		mlc_lifecycle_record(lifecycle);

	printf("MLC_INCOMPLETE reason=%d\n", record != NULL ? record->reason :
		MLC_INCOMPLETE_UNSUPPORTED_CONTEXT);
}

static int prepare_unified_snapshot(struct mlc_lifecycle_s *lifecycle,
		struct mlc_domain_guard_s *guard)
{
	size_t heap_index;

	for (heap_index = 0; heap_index < guard->heap_count; heap_index++) {
		if (get_node_cnt(guard->heaps[heap_index]) > MAX_ALLOC_COUNT) {
			mlc_lifecycle_fail(lifecycle, MLC_INCOMPLETE_CAPACITY);
			return ERROR;
		}
	}

	hash_reset();
	if (populate_snapshot_hash() != OK) {
		mlc_lifecycle_fail(lifecycle, MLC_INCOMPLETE_CAPACITY);
		return ERROR;
	}
	if (analyze_unified_snapshot() != OK) {
		mlc_lifecycle_fail(lifecycle, MLC_INCOMPLETE_BUDGET);
		return ERROR;
	}
	return OK;
}

static int run_mem_leak_checker_owned(struct mlc_lifecycle_s *lifecycle,
		struct mlc_invocation_report_s *report,
		struct mlc_domain_guard_s *guard, int checker_pid, const char *bin_name,
		const struct up_mem_leak_capture_s *capture)
{
	int leak_cnt = 0;
	int broken_cnt = 0;
	struct mm_heap_s *heap = NULL;
#ifdef CONFIG_APP_BINARY_SEPARATION
	const struct mm_loadable_domain_pin_s *domain = NULL;
#endif

	if (strncmp(bin_name, "kernel", strlen("kernel") + 1) == 0) {
		heap = kmm_get_baseheap();
	}
#ifdef CONFIG_APP_BINARY_SEPARATION
	else {
		domain = mlc_domain_guard_find_pin(guard, bin_name);
		heap = domain != NULL ? domain->heap : NULL;
	}
#endif

	if (!heap || prepare_unified_snapshot(lifecycle, guard) != OK) {
		if (heap == NULL) {
			mlc_lifecycle_fail(lifecycle, MLC_INCOMPLETE_UNSUPPORTED_CONTEXT);
		}
		return ERROR;
	}

	fill_hash_table(heap, &leak_cnt, &broken_cnt);

	/* Visit RAM region */
	if (ram_check(heap, checker_pid, guard,
#ifdef CONFIG_APP_BINARY_SEPARATION
		domain,
#endif
		&leak_cnt, capture) != OK) {
		mlc_lifecycle_fail(lifecycle, MLC_INCOMPLETE_TASK_CONTEXT);
		return ERROR;
	}

	if (capture_info(lifecycle, report, heap, bin_name, leak_cnt, broken_cnt) != OK) {
		mlc_lifecycle_fail(lifecycle, MLC_INCOMPLETE_CAPACITY);
		return ERROR;
	}

	return OK;
}

static int capture_heap_owned(struct mlc_lifecycle_s *lifecycle,
		struct mlc_invocation_report_s *report,
		struct mlc_domain_guard_s *guard, int checker_pid,
		struct mm_heap_s *heap, const char *name,
		const struct up_mem_leak_capture_s *capture)
{
	int leak_cnt = 0;
	int broken_cnt = 0;

	fill_hash_table(heap, &leak_cnt, &broken_cnt);
	if (ram_check(heap, checker_pid, guard,
#ifdef CONFIG_APP_BINARY_SEPARATION
		NULL,
#endif
		&leak_cnt, capture) != OK) {
		mlc_lifecycle_fail(lifecycle, MLC_INCOMPLETE_TASK_CONTEXT);
		return ERROR;
	}
	if (capture_info(lifecycle, report, heap, name, leak_cnt, broken_cnt) != OK) {
		mlc_lifecycle_fail(lifecycle, MLC_INCOMPLETE_CAPACITY);
		return ERROR;
	}
	return OK;
}

int run_mem_leak_checker(int checker_pid, char *bin_name)
{
	struct up_mem_leak_capture_s capture;
	struct mlc_lifecycle_s lifecycle;
	struct mlc_invocation_report_s report;
	struct mlc_domain_guard_s guard;
	size_t report_index;
	size_t guard_mark;
	int ret;

	up_mem_leak_capture_current(&capture);
	ret = mlc_lifecycle_begin(&lifecycle);
	if (ret < 0) {
		print_already_running();
		return ERROR;
	}
	g_active_budget = &lifecycle.counters;
	mlc_budget_bind(g_active_budget);
	if (mlc_lifecycle_set_epoch(&lifecycle,
		up_mem_leak_monotonic_usec()) < 0) {
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_TIMEOUT);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}
	if (!mlc_validate_current_capture(&capture)) {
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_TASK_CONTEXT);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}

	if (report_init(&lifecycle, &report, CONFIG_KMM_NHEAPS) != OK) {
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CAPACITY);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}
	if (hash_init(&lifecycle) != OK) {
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CAPACITY);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}
	guard_mark = mlc_lifecycle_mark(&lifecycle);
	ret = mlc_domain_guard_acquire(&lifecycle, &guard);
	if (ret < 0) {
		mlc_lifecycle_fail(&lifecycle, ret == -EUCLEAN ?
			MLC_INCOMPLETE_ROOTS : MLC_INCOMPLETE_CONTENTION);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}
	ret = mlc_collect_locked_candidates(&lifecycle, &guard, &report,
		&capture);
	if (ret != MLC_CANDIDATE_SNAPSHOT_OK) {
		mlc_clear_locked_candidates();
		mlc_lifecycle_fail(&lifecycle, mlc_candidate_failure_reason(ret));
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}

	ret = run_mem_leak_checker_owned(&lifecycle, &report, &guard, checker_pid,
		bin_name, &capture);
	if (ret == OK &&
		strncmp(bin_name, "kernel", strlen("kernel") + 1) == 0) {
		for (report_index = 1; report_index < CONFIG_KMM_NHEAPS;
			report_index++) {
			ret = capture_heap_owned(&lifecycle, &report, &guard,
				checker_pid, &kmm_get_baseheap()[report_index], "kernel",
				&capture);
			if (ret != OK) {
				break;
			}
		}
	}
	if (ret == OK) {
		if (mlc_lifecycle_advance(&lifecycle, MLC_PHASE_COPIED) < 0) {
			mlc_clear_locked_candidates();
			mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CAPACITY);
			print_incomplete(&lifecycle);
			mlc_clear_active_budget();
			return ERROR;
		}
#ifdef CONFIG_TC_KERNEL_MEM_LEAK_CHECKER
		notify_test_observer();
#endif
		if (mlc_domain_guard_release(&lifecycle, &guard, guard_mark) < 0) {
			mlc_clear_locked_candidates();
			mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_ROOTS);
			print_incomplete(&lifecycle);
			g_active_budget = NULL;
			mlc_budget_bind(NULL);
			return ERROR;
		}
		mlc_clear_locked_candidates();
		for (report_index = 0; report_index < report.heap_count;
			report_index++) {
			if (report_index == 0) {
				printf("\nKernel :\n");
			} else {
				printf("\nKernel[%u] :\n", (unsigned int)report_index);
			}
			print_heap_report(&report, &report.heaps[report_index]);
			print_extended_report_rows(&report, &report.heaps[report_index]);
		}
		mlc_lifecycle_complete(&lifecycle);
	} else {
		mlc_clear_locked_candidates();
		print_incomplete(&lifecycle);
	}
	mlc_clear_locked_candidates();
	mlc_clear_active_budget();
	return ret;
}

int run_all_mem_leak_checker_with_capture(int checker_pid,
		const struct up_mem_leak_capture_s *capture)
{
	struct mlc_lifecycle_s lifecycle;
	struct mlc_invocation_report_s report;
	struct mlc_domain_guard_s guard;
	int ret;
	size_t heap_capacity = CONFIG_KMM_NHEAPS;
	size_t report_index;
	size_t guard_mark;
#ifdef CONFIG_APP_BINARY_SEPARATION
	size_t app_report_index;
#endif

#ifdef CONFIG_APP_BINARY_SEPARATION
	heap_capacity += MLC_DOMAIN_PIN_CAPACITY;
#endif

	ret = mlc_lifecycle_begin(&lifecycle);
	if (ret < 0) {
		print_already_running();
		return ERROR;
	}
	g_active_budget = &lifecycle.counters;
	mlc_budget_bind(g_active_budget);
	if (mlc_lifecycle_set_epoch(&lifecycle,
		up_mem_leak_monotonic_usec()) < 0) {
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_TIMEOUT);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}
	if (!mlc_validate_current_capture(capture)) {
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_TASK_CONTEXT);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}

	if (report_init(&lifecycle, &report, heap_capacity) != OK) {
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CAPACITY);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}
	if (hash_init(&lifecycle) != OK) {
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CAPACITY);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}
	guard_mark = mlc_lifecycle_mark(&lifecycle);
	ret = mlc_domain_guard_acquire(&lifecycle, &guard);
	if (ret < 0) {
		mlc_lifecycle_fail(&lifecycle, ret == -EUCLEAN ?
			MLC_INCOMPLETE_ROOTS : MLC_INCOMPLETE_CONTENTION);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}
	ret = mlc_collect_locked_candidates(&lifecycle, &guard, &report, capture);
	if (ret != MLC_CANDIDATE_SNAPSHOT_OK) {
		mlc_clear_locked_candidates();
		mlc_lifecycle_fail(&lifecycle, mlc_candidate_failure_reason(ret));
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}

	ret = run_mem_leak_checker_owned(&lifecycle, &report, &guard, checker_pid,
		"kernel", capture);
	if (ret == OK) {
		for (report_index = 1; report_index < CONFIG_KMM_NHEAPS;
			report_index++) {
			ret = capture_heap_owned(&lifecycle, &report, &guard,
				checker_pid, &kmm_get_baseheap()[report_index], "kernel",
				capture);
			if (ret != OK) {
				break;
			}
		}
	}

	if (ret != OK) {
		mlc_clear_locked_candidates();
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}
#ifdef CONFIG_APP_BINARY_SEPARATION
	for (report_index = 0; report_index < guard.report_pin_count;
		report_index++) {
		if (guard.pins[report_index].heap != NULL) {
			ret = capture_heap_owned(&lifecycle, &report, &guard, checker_pid,
				guard.pins[report_index].heap,
				guard.pins[report_index].name, capture);
			if (ret == OK && report.heap_count > 0) {
				report.heaps[report.heap_count - 1].text_start =
					guard.pins[report_index].text_start;
				report.heaps[report.heap_count - 1].text_size =
					guard.pins[report_index].text_size;
			}
			if (ret != OK) {
				mlc_clear_locked_candidates();
				print_incomplete(&lifecycle);
				g_active_budget = NULL;
				mlc_budget_bind(NULL);
				return ERROR;
			}
		}
	}
#endif
	if (mlc_lifecycle_advance(&lifecycle, MLC_PHASE_COPIED) < 0) {
		mlc_clear_locked_candidates();
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CAPACITY);
		print_incomplete(&lifecycle);
		g_active_budget = NULL;
		mlc_budget_bind(NULL);
		return ERROR;
	}
	#ifdef CONFIG_TC_KERNEL_MEM_LEAK_CHECKER
	notify_test_observer();
	#endif
	if (mlc_domain_guard_release(&lifecycle, &guard, guard_mark) < 0) {
		mlc_clear_locked_candidates();
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_ROOTS);
		print_incomplete(&lifecycle);
		mlc_clear_active_budget();
		return ERROR;
	}
	mlc_clear_locked_candidates();
	printf("\nKernel :\n");
	for (report_index = 0; report_index < CONFIG_KMM_NHEAPS;
		report_index++) {
		if (report_index > 0) {
			printf("\nKernel[%u] :\n", (unsigned int)report_index);
		}
		print_heap_report(&report, &report.heaps[report_index]);
		print_extended_report_rows(&report, &report.heaps[report_index]);
	}
#ifdef CONFIG_APP_BINARY_SEPARATION
	printf("\nBelow are text addresses of loadable apps (and common binary if enabled) :\n");
	printf("The pc value of the allocation can be obtained by subtracting the text start address of the appropriate binary\n\n");
	app_report_index = CONFIG_KMM_NHEAPS;
	for (report_index = 0; report_index < guard.report_pin_count;
		report_index++) {
		if (guard.pins[report_index].heap != NULL) {
			if (app_report_index < report.heap_count &&
				report.heaps[app_report_index].text_start != 0) {
				printf("[%s] Text Addr : %p, Text Size : %u\n",
					report.heaps[app_report_index].name,
					(void *)report.heaps[app_report_index].text_start,
					(unsigned int)report.heaps[app_report_index].text_size);
			}
			app_report_index++;
		}
	}
	printf("\n");
	for (report_index = CONFIG_KMM_NHEAPS; report_index < report.heap_count;
		report_index++) {
		printf("%s :\n", report.heaps[report_index].name);
		print_heap_report(&report, &report.heaps[report_index]);
		print_extended_report_rows(&report, &report.heaps[report_index]);
	}
#else
	(void)report_index;
#endif
	mlc_clear_locked_candidates();
	mlc_lifecycle_complete(&lifecycle);
	mlc_clear_active_budget();
	return OK;
}

#if !defined(CONFIG_ARCH_CHIP_LM) && !defined(CONFIG_ARCH_CHIP_AMEBASMART)
int run_all_mem_leak_checker(int checker_pid)
{
	struct up_mem_leak_capture_s capture;

	up_mem_leak_capture_current(&capture);
	return run_all_mem_leak_checker_with_capture(checker_pid, &capture);
}
#endif
