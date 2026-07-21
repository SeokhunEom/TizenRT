#ifndef __KERNEL_DEBUG_MEM_LEAK_CHECKER_LIFECYCLE_H
#define __KERNEL_DEBUG_MEM_LEAK_CHECKER_LIFECYCLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mem_leak_checker_budget.h"

#define MLC_LEDGER_CAPACITY 12

enum mlc_snapshot_phase_e {
	MLC_PHASE_IDLE = 0,
	MLC_PHASE_ADMITTED,
	MLC_PHASE_WORKSPACE,
	MLC_PHASE_DOMAIN,
	MLC_PHASE_CRITICAL,
	MLC_PHASE_HEAPS,
	MLC_PHASE_CAPTURED,
	MLC_PHASE_PAUSED,
	MLC_PHASE_ANALYSIS,
	MLC_PHASE_COPIED,
	MLC_PHASE_RELEASED
};

enum mlc_incomplete_reason_e {
	MLC_INCOMPLETE_NONE = 0,
	MLC_INCOMPLETE_ROOTS,
	MLC_INCOMPLETE_CAPACITY,
	MLC_INCOMPLETE_CONTENTION,
	MLC_INCOMPLETE_TIMEOUT,
	MLC_INCOMPLETE_TASK_CONTEXT,
	MLC_INCOMPLETE_UNSUPPORTED_CONTEXT,
	MLC_INCOMPLETE_DOMAIN_CHANGED,
	MLC_INCOMPLETE_HEAP_CORRUPT,
	MLC_INCOMPLETE_BUDGET,
	MLC_FATAL_RESUME_AMBIGUOUS,
	MLC_INCOMPLETE_PREOWNED_RESOURCE,
	MLC_INCOMPLETE_GENERATION_EXHAUSTED,
	MLC_INCOMPLETE_CLOCK,
	MLC_INCOMPLETE_DEADLINE,
	MLC_INCOMPLETE_BUSY_REGISTRY,
	MLC_INCOMPLETE_BUSY_CRITICAL,
	MLC_INCOMPLETE_BUSY_HEAP,
	MLC_INCOMPLETE_INTERNAL
};

enum mlc_resource_e {
	MLC_RESOURCE_WORKSPACE = 0,
	MLC_RESOURCE_CRITICAL,
	MLC_RESOURCE_HEAP,
	MLC_RESOURCE_DOMAIN,
	MLC_RESOURCE_PAUSE
};

typedef void (*mlc_cleanup_t)(void *arg);
typedef uint64_t (*mlc_clock_read_t)(void *arg);
typedef void (*mlc_fatal_handler_t)(enum mlc_incomplete_reason_e reason,
		void *arg);

struct mlc_ledger_entry_s {
	enum mlc_resource_e resource;
	enum mlc_snapshot_phase_e phase;
	mlc_cleanup_t cleanup;
	void *arg;
};

struct mlc_post_release_record_s {
	enum mlc_incomplete_reason_e reason;
	enum mlc_snapshot_phase_e terminal_phase;
	struct mlc_ledger_entry_s terminal_ledger[MLC_LEDGER_CAPACITY];
	size_t terminal_resources;
	size_t released_resources;
	size_t discarded_rows;
	bool verdict_allowed;
	bool valid;
};

struct mlc_provisional_report_s {
	void *rows;
	size_t capacity;
	size_t row_size;
	size_t count;
	bool sealed;
};

struct mlc_lifecycle_s {
	struct mlc_ledger_entry_s entries[MLC_LEDGER_CAPACITY];
	size_t count;
	enum mlc_snapshot_phase_e phase;
	enum mlc_incomplete_reason_e reason;
	size_t provisional_rows;
	bool admitted;
	bool verdict_allowed;
	struct mlc_provisional_report_s report;
	struct mlc_post_release_record_s record;
	uint64_t epoch_usec;
	struct mlc_budget_counters_s counters;
};

enum mlc_budget_state_e {
	MLC_BUDGET_WORK = 0,
	MLC_BUDGET_EXHAUSTED,
	MLC_BUDGET_RESUME_REQUESTED,
	MLC_BUDGET_FATAL
};

struct mlc_budget_s {
	mlc_clock_read_t read;
	void *clock_arg;
	uint64_t operations_left;
	uint64_t work_deadline;
	uint64_t resume_deadline;
	enum mlc_budget_state_e state;
};

int mlc_lifecycle_begin(struct mlc_lifecycle_s *lifecycle);
int mlc_lifecycle_set_epoch(struct mlc_lifecycle_s *lifecycle,
		uint64_t epoch_usec);
size_t mlc_lifecycle_mark(const struct mlc_lifecycle_s *lifecycle);
int mlc_lifecycle_advance(struct mlc_lifecycle_s *lifecycle,
		enum mlc_snapshot_phase_e phase);
int mlc_lifecycle_push(struct mlc_lifecycle_s *lifecycle,
		enum mlc_snapshot_phase_e phase, enum mlc_resource_e resource,
		mlc_cleanup_t cleanup, void *arg);
void mlc_lifecycle_unwind_to(struct mlc_lifecycle_s *lifecycle, size_t mark);
void mlc_lifecycle_fail(struct mlc_lifecycle_s *lifecycle,
		enum mlc_incomplete_reason_e reason);
void mlc_lifecycle_complete(struct mlc_lifecycle_s *lifecycle);
void mlc_lifecycle_add_provisional_row(struct mlc_lifecycle_s *lifecycle);
int mlc_lifecycle_bind_report(struct mlc_lifecycle_s *lifecycle, void *rows,
		size_t capacity, size_t row_size);
int mlc_lifecycle_store_provisional(struct mlc_lifecycle_s *lifecycle,
		const void *row);
void mlc_lifecycle_invoke_fatal(struct mlc_lifecycle_s *lifecycle,
		mlc_fatal_handler_t handler, void *arg);
const struct mlc_post_release_record_s *mlc_lifecycle_record(
		const struct mlc_lifecycle_s *lifecycle);

int mlc_budget_start(struct mlc_budget_s *budget, mlc_clock_read_t read,
		void *clock_arg, uint64_t operations, uint64_t work_window,
		uint64_t resume_window);
int mlc_budget_consume(struct mlc_budget_s *budget, uint64_t operations);
int mlc_budget_request_resume(struct mlc_budget_s *budget);
int mlc_lifecycle_budget_take(struct mlc_lifecycle_s *lifecycle,
		enum mlc_budget_counter_e counter, size_t operations);
int mlc_lifecycle_budget_chunk_begin(struct mlc_lifecycle_s *lifecycle,
		enum mlc_budget_counter_e counter, size_t operations,
		uint64_t now_usec);
int mlc_lifecycle_budget_chunk_end(const struct mlc_lifecycle_s *lifecycle,
		uint64_t now_usec);

#endif
