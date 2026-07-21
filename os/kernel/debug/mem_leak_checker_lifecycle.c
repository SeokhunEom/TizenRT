#include <tinyara/config.h>

#include <errno.h>
#include <string.h>

#include <tinyara/spinlock.h>
#include <tinyara/clock.h>

#include "mem_leak_checker_lifecycle.h"

static volatile spinlock_t g_mem_leak_checker_admission = SP_UNLOCKED;

static void mlc_lifecycle_release_admission(struct mlc_lifecycle_s *lifecycle)
{
	if (lifecycle->admitted) {
		if (mlc_budget_current() == &lifecycle->counters) {
			mlc_budget_bind(NULL);
		}
		lifecycle->admitted = false;
		SP_DMB();
		g_mem_leak_checker_admission = SP_UNLOCKED;
		SP_DSB();
		SP_SEV();
	}
}

int mlc_lifecycle_begin(struct mlc_lifecycle_s *lifecycle)
{
	if (lifecycle == NULL) {
		return -EINVAL;
	}

	memset(lifecycle, 0, sizeof(*lifecycle));
	if (up_testset(&g_mem_leak_checker_admission) == SP_LOCKED) {
		return -EBUSY;
	}
	if (mlc_budget_counters_init(&lifecycle->counters) < 0) {
		g_mem_leak_checker_admission = SP_UNLOCKED;
		SP_DSB();
		SP_SEV();
		return -ERANGE;
	}
	/* Publish only after admission and budget initialization.  Pause service
	 * callbacks therefore cannot observe a NULL budget during this lifecycle. */
	mlc_budget_bind(&lifecycle->counters);

	lifecycle->admitted = true;
	lifecycle->verdict_allowed = true;
	lifecycle->phase = MLC_PHASE_ADMITTED;
	return 0;
}

int mlc_lifecycle_set_epoch(struct mlc_lifecycle_s *lifecycle,
		uint64_t epoch_usec)
{
	if (lifecycle == NULL || !lifecycle->admitted || epoch_usec == 0) {
		return -EINVAL;
	}
	lifecycle->epoch_usec = epoch_usec;
	return mlc_budget_set_epoch(&lifecycle->counters, epoch_usec,
			78000u, 95000u);
}

__attribute__((weak)) uint64_t up_mem_leak_monotonic_usec(void)
{
	return (uint64_t)TICK2USEC(clock_systimer());
}

size_t mlc_lifecycle_mark(const struct mlc_lifecycle_s *lifecycle)
{
	return lifecycle->count;
}

static bool mlc_lifecycle_resource_matches(enum mlc_snapshot_phase_e phase,
		enum mlc_resource_e resource)
{
	switch (resource) {
	case MLC_RESOURCE_WORKSPACE:
		return phase == MLC_PHASE_WORKSPACE;
	case MLC_RESOURCE_DOMAIN:
		return phase == MLC_PHASE_DOMAIN;
	case MLC_RESOURCE_CRITICAL:
		return phase == MLC_PHASE_CRITICAL;
	case MLC_RESOURCE_HEAP:
		return phase == MLC_PHASE_HEAPS;
	case MLC_RESOURCE_PAUSE:
		return phase == MLC_PHASE_PAUSED;
	default:
		return false;
	}
}

static bool mlc_lifecycle_has_resource(const struct mlc_lifecycle_s *lifecycle,
		enum mlc_resource_e resource)
{
	size_t index;

	for (index = 0; index < lifecycle->count; index++) {
		if (lifecycle->entries[index].resource == resource) {
			return true;
		}
	}
	return false;
}

static bool mlc_lifecycle_can_push(const struct mlc_lifecycle_s *lifecycle,
		enum mlc_snapshot_phase_e phase, enum mlc_resource_e resource)
{
	switch (resource) {
	case MLC_RESOURCE_WORKSPACE:
		return phase == MLC_PHASE_WORKSPACE &&
			(lifecycle->phase == MLC_PHASE_ADMITTED ||
			 lifecycle->phase == MLC_PHASE_WORKSPACE);
	case MLC_RESOURCE_DOMAIN:
		return phase == MLC_PHASE_DOMAIN &&
			(lifecycle->phase == MLC_PHASE_ADMITTED ||
			 lifecycle->phase == MLC_PHASE_WORKSPACE ||
			 lifecycle->phase == MLC_PHASE_DOMAIN);
	case MLC_RESOURCE_CRITICAL:
		return phase == MLC_PHASE_CRITICAL &&
			lifecycle->phase == MLC_PHASE_DOMAIN &&
			mlc_lifecycle_has_resource(lifecycle, MLC_RESOURCE_DOMAIN);
	case MLC_RESOURCE_HEAP:
		return phase == MLC_PHASE_HEAPS &&
			(lifecycle->phase == MLC_PHASE_CRITICAL ||
			 lifecycle->phase == MLC_PHASE_HEAPS) &&
			mlc_lifecycle_has_resource(lifecycle, MLC_RESOURCE_CRITICAL);
	case MLC_RESOURCE_PAUSE:
		return phase == MLC_PHASE_PAUSED &&
			lifecycle->phase == MLC_PHASE_CAPTURED &&
			mlc_lifecycle_has_resource(lifecycle, MLC_RESOURCE_HEAP);
	default:
		return false;
	}
}

int mlc_lifecycle_advance(struct mlc_lifecycle_s *lifecycle,
		enum mlc_snapshot_phase_e phase)
{
	if (lifecycle == NULL || !lifecycle->admitted) {
		return -EINVAL;
	}
	if (!((phase == MLC_PHASE_CAPTURED &&
		  lifecycle->phase == MLC_PHASE_HEAPS &&
		  mlc_lifecycle_has_resource(lifecycle, MLC_RESOURCE_HEAP)) ||
		 (phase == MLC_PHASE_ANALYSIS &&
		  lifecycle->phase == MLC_PHASE_PAUSED &&
		  mlc_lifecycle_has_resource(lifecycle, MLC_RESOURCE_PAUSE)) ||
		 (phase == MLC_PHASE_COPIED &&
		  lifecycle->phase == MLC_PHASE_ANALYSIS))) {
		return -EINVAL;
	}

	lifecycle->phase = phase;
	return 0;
}

int mlc_lifecycle_push(struct mlc_lifecycle_s *lifecycle,
		enum mlc_snapshot_phase_e phase, enum mlc_resource_e resource,
		mlc_cleanup_t cleanup, void *arg)
{
	struct mlc_ledger_entry_s *entry;

	if (lifecycle == NULL || !lifecycle->admitted || cleanup == NULL ||
		!mlc_lifecycle_resource_matches(phase, resource) ||
		!mlc_lifecycle_can_push(lifecycle, phase, resource)) {
		return -EINVAL;
	}

	if (lifecycle->count >= MLC_LEDGER_CAPACITY) {
		return -ENOSPC;
	}
	lifecycle->phase = phase;

	entry = &lifecycle->entries[lifecycle->count++];
	entry->resource = resource;
	entry->phase = phase;
	entry->cleanup = cleanup;
	entry->arg = arg;
	lifecycle->phase = phase;
	return 0;
}

static void mlc_lifecycle_record_terminal(struct mlc_lifecycle_s *lifecycle,
		enum mlc_incomplete_reason_e reason, bool verdict_allowed,
		size_t discarded_rows)
{
	lifecycle->record.reason = reason;
	lifecycle->record.terminal_phase = lifecycle->phase;
	lifecycle->record.terminal_resources = lifecycle->count;
	memcpy(lifecycle->record.terminal_ledger, lifecycle->entries,
		lifecycle->count * sizeof(lifecycle->entries[0]));
	lifecycle->record.discarded_rows = discarded_rows;
	lifecycle->record.verdict_allowed = verdict_allowed;
	lifecycle->record.valid = true;
}

static void mlc_lifecycle_discard_report(struct mlc_lifecycle_s *lifecycle)
{
	if (lifecycle->report.rows != NULL && lifecycle->report.count > 0) {
		memset(lifecycle->report.rows, 0,
			lifecycle->report.count * lifecycle->report.row_size);
	}
	lifecycle->report.count = 0;
	lifecycle->report.sealed = false;
}

void mlc_lifecycle_unwind_to(struct mlc_lifecycle_s *lifecycle, size_t mark)
{
	while (lifecycle->count > mark) {
		struct mlc_ledger_entry_s *entry =
			&lifecycle->entries[lifecycle->count - 1];

		entry->cleanup(entry->arg);
		lifecycle->count--;
		lifecycle->record.released_resources++;
		memset(entry, 0, sizeof(*entry));
	}

	lifecycle->phase = lifecycle->count > 0 ?
		lifecycle->entries[lifecycle->count - 1].phase : MLC_PHASE_ADMITTED;
}

void mlc_lifecycle_fail(struct mlc_lifecycle_s *lifecycle,
		enum mlc_incomplete_reason_e reason)
{
	if (lifecycle->reason == MLC_INCOMPLETE_NONE) {
		lifecycle->reason = reason;
	}
	lifecycle->verdict_allowed = false;
	mlc_lifecycle_record_terminal(lifecycle, lifecycle->reason, false,
		lifecycle->provisional_rows);
	mlc_lifecycle_discard_report(lifecycle);
	lifecycle->provisional_rows = 0;
	mlc_lifecycle_unwind_to(lifecycle, 0);
	mlc_lifecycle_release_admission(lifecycle);
	lifecycle->phase = MLC_PHASE_IDLE;
}

void mlc_lifecycle_complete(struct mlc_lifecycle_s *lifecycle)
{
	lifecycle->report.sealed = lifecycle->verdict_allowed;
	mlc_lifecycle_record_terminal(lifecycle, lifecycle->reason,
		lifecycle->verdict_allowed, 0);
	mlc_lifecycle_unwind_to(lifecycle, 0);
	mlc_lifecycle_release_admission(lifecycle);
	lifecycle->phase = MLC_PHASE_IDLE;
}

void mlc_lifecycle_add_provisional_row(struct mlc_lifecycle_s *lifecycle)
{
	lifecycle->provisional_rows++;
}

int mlc_lifecycle_bind_report(struct mlc_lifecycle_s *lifecycle, void *rows,
		size_t capacity, size_t row_size)
{
	if (lifecycle == NULL || !lifecycle->admitted || rows == NULL ||
		capacity == 0 || row_size == 0 || capacity > SIZE_MAX / row_size ||
		lifecycle->report.rows != NULL) {
		return -EINVAL;
	}

	lifecycle->report.rows = rows;
	lifecycle->report.capacity = capacity;
	lifecycle->report.row_size = row_size;
	return 0;
}

int mlc_lifecycle_store_provisional(struct mlc_lifecycle_s *lifecycle,
		const void *row)
{
	unsigned char *destination;

	if (lifecycle == NULL || row == NULL || !lifecycle->admitted ||
		!lifecycle->verdict_allowed || lifecycle->report.rows == NULL ||
		lifecycle->report.sealed) {
		return -EINVAL;
	}
	if (lifecycle->report.count >= lifecycle->report.capacity) {
		return -ENOSPC;
	}

	destination = (unsigned char *)lifecycle->report.rows +
		lifecycle->report.count * lifecycle->report.row_size;
	memcpy(destination, row, lifecycle->report.row_size);
	lifecycle->report.count++;
	lifecycle->provisional_rows++;
	return 0;
}

void mlc_lifecycle_invoke_fatal(struct mlc_lifecycle_s *lifecycle,
		mlc_fatal_handler_t handler, void *arg)
{
	lifecycle->reason = MLC_FATAL_RESUME_AMBIGUOUS;
	lifecycle->verdict_allowed = false;
	mlc_lifecycle_record_terminal(lifecycle, lifecycle->reason, false,
		lifecycle->provisional_rows);
	mlc_lifecycle_discard_report(lifecycle);
	lifecycle->provisional_rows = 0;
	handler(lifecycle->reason, arg);
}

const struct mlc_post_release_record_s *mlc_lifecycle_record(
		const struct mlc_lifecycle_s *lifecycle)
{
	return lifecycle->record.valid ? &lifecycle->record : NULL;
}

int mlc_budget_start(struct mlc_budget_s *budget, mlc_clock_read_t read,
		void *clock_arg, uint64_t operations, uint64_t work_window,
		uint64_t resume_window)
{
	uint64_t now;

	if (budget == NULL || read == NULL || work_window > resume_window) {
		return -EINVAL;
	}

	now = read(clock_arg);
	if (UINT64_MAX - now < resume_window) {
		return -EOVERFLOW;
	}

	budget->read = read;
	budget->clock_arg = clock_arg;
	budget->operations_left = operations;
	budget->work_deadline = now + work_window;
	budget->resume_deadline = now + resume_window;
	budget->state = MLC_BUDGET_WORK;
	return 0;
}

int mlc_budget_consume(struct mlc_budget_s *budget, uint64_t operations)
{
	if (budget == NULL || budget->state != MLC_BUDGET_WORK) {
		return -EINVAL;
	}

	if (budget->read(budget->clock_arg) >= budget->work_deadline) {
		budget->state = MLC_BUDGET_EXHAUSTED;
		return -ETIME;
	}

	if (operations > budget->operations_left) {
		budget->state = MLC_BUDGET_EXHAUSTED;
		return -E2BIG;
	}

	budget->operations_left -= operations;
	return 0;
}

int mlc_budget_request_resume(struct mlc_budget_s *budget)
{
	if (budget == NULL || budget->read == NULL) {
		return -EINVAL;
	}

	if (budget->read(budget->clock_arg) > budget->resume_deadline) {
		budget->state = MLC_BUDGET_FATAL;
		return -ETIME;
	}

	budget->state = MLC_BUDGET_RESUME_REQUESTED;
	return 0;
}

int mlc_lifecycle_budget_take(struct mlc_lifecycle_s *lifecycle,
		enum mlc_budget_counter_e counter, size_t operations)
{
	if (lifecycle == NULL || !lifecycle->admitted) {
		return -EINVAL;
	}
	return mlc_budget_counter_take(&lifecycle->counters, counter, operations);
}

int mlc_lifecycle_budget_chunk_begin(struct mlc_lifecycle_s *lifecycle,
		enum mlc_budget_counter_e counter, size_t operations,
		uint64_t now_usec)
{
	if (lifecycle == NULL || !lifecycle->admitted) {
		return -EINVAL;
	}
	return mlc_budget_chunk_begin(&lifecycle->counters, counter, operations,
		now_usec);
}

int mlc_lifecycle_budget_chunk_end(const struct mlc_lifecycle_s *lifecycle,
		uint64_t now_usec)
{
	if (lifecycle == NULL || !lifecycle->admitted) {
		return -EINVAL;
	}
	return mlc_budget_chunk_end(&lifecycle->counters, now_usec);
}
