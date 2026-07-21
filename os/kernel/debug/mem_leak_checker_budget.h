/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 ****************************************************************************/

#ifndef __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_BUDGET_H
#define __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_BUDGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MLC_SNAPSHOT_REQUESTED_BYTES_MAX 1048576u
#define MLC_BUDGET_COUNTER_MAX 65536u
#define MLC_BUDGET_CHUNK_MAX 256u

#ifndef CONFIG_MEM_LEAK_CHECKER_REGISTRY_ENUM_MAX
#define CONFIG_MEM_LEAK_CHECKER_REGISTRY_ENUM_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_DOMAIN_PIN_MAX
#define CONFIG_MEM_LEAK_CHECKER_DOMAIN_PIN_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_ROOT_CONTAINER_ENUM_MAX
#define CONFIG_MEM_LEAK_CHECKER_ROOT_CONTAINER_ENUM_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_HEAP_ACQUIRE_MAX
#define CONFIG_MEM_LEAK_CHECKER_HEAP_ACQUIRE_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_HEAP_RELEASE_VALIDATE_MAX
#define CONFIG_MEM_LEAK_CHECKER_HEAP_RELEASE_VALIDATE_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_DOMAIN_UNPIN_MAX
#define CONFIG_MEM_LEAK_CHECKER_DOMAIN_UNPIN_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_REGISTRY_UNWIND_MAX
#define CONFIG_MEM_LEAK_CHECKER_REGISTRY_UNWIND_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_PAUSE_ACK_MAX
#define CONFIG_MEM_LEAK_CHECKER_PAUSE_ACK_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_REMOTE_PAUSED_SERVICE_MAX
#define CONFIG_MEM_LEAK_CHECKER_REMOTE_PAUSED_SERVICE_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_CANCEL_COMPLETION_MAX
#define CONFIG_MEM_LEAK_CHECKER_CANCEL_COMPLETION_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_RESUME_COMPLETION_MAX
#define CONFIG_MEM_LEAK_CHECKER_RESUME_COMPLETION_MAX 65536u
#endif
#ifndef CONFIG_MEM_LEAK_CHECKER_SGI_DRAIN_MAX
#define CONFIG_MEM_LEAK_CHECKER_SGI_DRAIN_MAX 65536u
#endif

enum mlc_budget_counter_e {
	MLC_BUDGET_REGISTRY_ENUM,
	MLC_BUDGET_DOMAIN_PIN,
	MLC_BUDGET_ROOT_CONTAINER_ENUM,
	MLC_BUDGET_HEAP_ACQUIRE,
	MLC_BUDGET_HEAP_RELEASE_VALIDATE,
	MLC_BUDGET_DOMAIN_UNPIN,
	MLC_BUDGET_REGISTRY_UNWIND,
	MLC_BUDGET_HEAP_REGION,
	MLC_BUDGET_REGION_BYTES,
	MLC_BUDGET_HEAP_NODE,
	MLC_BUDGET_SENTINEL_VALIDATE,
	MLC_BUDGET_LINK_VALIDATE,
	MLC_BUDGET_FREE_NODE,
	MLC_BUDGET_ALLOCATED_NODE,
	MLC_BUDGET_DOMAIN_REVALIDATE,
	MLC_BUDGET_HEAP_REVALIDATE,
	MLC_BUDGET_TASK_REVALIDATE,
	MLC_BUDGET_IRQ_REVALIDATE,
	MLC_BUDGET_EXCLUSION,
	MLC_BUDGET_DEDUP,
	MLC_BUDGET_INDEX_SORT_COMPARE,
	MLC_BUDGET_INDEX_SORT_MOVE,
	MLC_BUDGET_CANDIDATE,
	MLC_BUDGET_ROOT_RANGE,
	MLC_BUDGET_POINTER_WINDOW,
	MLC_BUDGET_EXACT_LOOKUP,
	MLC_BUDGET_INTERIOR_LOOKUP,
	MLC_BUDGET_FRONTIER_POP,
	MLC_BUDGET_TARJAN_FRAME,
	MLC_BUDGET_EDGE_RESCAN,
	MLC_BUDGET_REPORT_ROW,
	MLC_BUDGET_COPY_BYTES,
	MLC_BUDGET_PAUSE_ACK,
	MLC_BUDGET_REMOTE_PAUSED_SERVICE,
	MLC_BUDGET_CANCEL_COMPLETION,
	MLC_BUDGET_RESUME_COMPLETION,
	MLC_BUDGET_SGI_DRAIN,
	MLC_BUDGET_COUNTER_COUNT
};

struct mlc_budget_region_s {
	size_t bytes;
};

struct mlc_budget_counters_s {
	uint32_t configured[MLC_BUDGET_COUNTER_COUNT];
	uint32_t remaining[MLC_BUDGET_COUNTER_COUNT];
	size_t ledger_capacity;
	size_t ledger_available;
	size_t ledger_committed;
	size_t reverse_heap_available;
	size_t reverse_domain_available;
	size_t reverse_registry_available;
	uint64_t requested_payload_bytes;
	uint64_t epoch_usec;
	uint64_t work_deadline_usec;
	uint64_t resume_deadline_usec;
	bool epoch_valid;
	uint32_t ledger_reserved[MLC_BUDGET_COUNTER_COUNT];
	uint32_t ledger_committed_by_kind[MLC_BUDGET_COUNTER_COUNT];
};

int mlc_budget_counters_init(struct mlc_budget_counters_s *budget);
void mlc_budget_bind(struct mlc_budget_counters_s *budget);
struct mlc_budget_counters_s *mlc_budget_current(void);
int mlc_budget_counter_take(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e counter, size_t operations);
uint64_t mlc_budget_clock_now(void);
uint64_t up_mem_leak_monotonic_usec(void);
int mlc_budget_chunk_begin(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e counter, size_t operations,
		uint64_t now_usec);
int mlc_budget_chunk_end(const struct mlc_budget_counters_s *budget,
		uint64_t now_usec);
int mlc_budget_chunk_begin_resume(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e counter, size_t operations,
		uint64_t now_usec);
int mlc_budget_chunk_end_resume(const struct mlc_budget_counters_s *budget,
		uint64_t now_usec);
int mlc_budget_set_epoch(struct mlc_budget_counters_s *budget,
		uint64_t epoch_usec, uint64_t work_window_usec,
		uint64_t resume_window_usec);
int mlc_budget_add_requested_bytes(struct mlc_budget_counters_s *budget,
		size_t requested_bytes);
int mlc_budget_derive_region_nodes(struct mlc_budget_counters_s *budget,
		const struct mlc_budget_region_s *regions, size_t region_count,
		size_t min_chunk, size_t *node_ceiling);
int mlc_budget_reserve_ownership(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse);
int mlc_budget_commit_ownership(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward);
int mlc_budget_reserve_ownership_identity(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse,
		uint64_t identity);
int mlc_budget_commit_ownership_identity(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, uint64_t identity);
int mlc_budget_return_reservation(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse);
int mlc_budget_return_reservation_identity(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse,
		uint64_t identity);
int mlc_budget_release_ownership(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse);
int mlc_budget_release_ownership_identity(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse,
		uint64_t identity);

#endif
