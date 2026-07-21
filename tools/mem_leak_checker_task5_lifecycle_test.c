#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "mem_leak_checker_lifecycle.h"

struct cleanup_trace_s {
	unsigned int order[12];
	unsigned int count;
};

struct cleanup_arg_s {
	struct cleanup_trace_s *trace;
	unsigned int value;
};

static void record_cleanup(void *arg)
{
	struct cleanup_arg_s *cleanup = arg;

	cleanup->trace->order[cleanup->trace->count++] = cleanup->value;
}

static uint64_t fake_clock(void *arg)
{
	return *(uint64_t *)arg;
}

static void fatal_stub(enum mlc_incomplete_reason_e reason, void *arg)
{
	bool *called = arg;

	assert(reason == MLC_FATAL_RESUME_AMBIGUOUS);
	*called = true;
}

static void test_all_recoverable_failures_and_record_lifetime(void)
{
	enum mlc_incomplete_reason_e reasons[] = {
		MLC_INCOMPLETE_ROOTS,
		MLC_INCOMPLETE_CAPACITY,
		MLC_INCOMPLETE_CONTENTION,
		MLC_INCOMPLETE_TIMEOUT,
		MLC_INCOMPLETE_UNSUPPORTED_CONTEXT
	};
	enum mlc_snapshot_phase_e phases[] = {
		MLC_PHASE_WORKSPACE,
		MLC_PHASE_DOMAIN,
		MLC_PHASE_CRITICAL,
		MLC_PHASE_HEAPS,
		MLC_PHASE_PAUSED
	};
	enum mlc_resource_e resources[] = {
		MLC_RESOURCE_WORKSPACE,
		MLC_RESOURCE_DOMAIN,
		MLC_RESOURCE_CRITICAL,
		MLC_RESOURCE_HEAP,
		MLC_RESOURCE_PAUSE
	};
	unsigned int reason_index;

	for (reason_index = 0;
		reason_index < sizeof(reasons) / sizeof(reasons[0]);
		reason_index++) {
		struct mlc_lifecycle_s lifecycle;
		struct cleanup_trace_s trace = { { 0 }, 0 };
		struct cleanup_arg_s args[5];
		const struct mlc_post_release_record_s *record;
		unsigned int i;

		assert(mlc_lifecycle_begin(&lifecycle) == 0);
		for (i = 0; i < 4; i++) {
			args[i].trace = &trace;
			args[i].value = i + 1;
			assert(mlc_lifecycle_push(&lifecycle, phases[i], resources[i],
					record_cleanup, &args[i]) == 0);
		}
		assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_CAPTURED) == 0);
		args[4].trace = &trace;
		args[4].value = 5;
		assert(mlc_lifecycle_push(&lifecycle, phases[4], resources[4],
			record_cleanup, &args[4]) == 0);
		assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_ANALYSIS) == 0);
		assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_COPIED) == 0);
		mlc_lifecycle_add_provisional_row(&lifecycle);
		mlc_lifecycle_add_provisional_row(&lifecycle);
		mlc_lifecycle_add_provisional_row(&lifecycle);
		mlc_lifecycle_fail(&lifecycle, reasons[reason_index]);
		assert(trace.count == 5);
		for (i = 0; i < 5; i++) {
			assert(trace.order[i] == 5 - i);
		}
		assert(lifecycle.count == 0 && lifecycle.provisional_rows == 0);
		assert(!lifecycle.admitted && !lifecycle.verdict_allowed);
		assert(lifecycle.reason == reasons[reason_index]);
		assert(lifecycle.phase == MLC_PHASE_IDLE);
		record = mlc_lifecycle_record(&lifecycle);
		assert(record != NULL && record->valid);
		assert(record->reason == reasons[reason_index]);
		assert(record->terminal_phase == MLC_PHASE_COPIED);
		assert(record->terminal_resources == 5);
		assert(record->terminal_ledger[0].resource == MLC_RESOURCE_WORKSPACE);
		assert(record->terminal_ledger[1].resource == MLC_RESOURCE_DOMAIN);
		assert(record->terminal_ledger[2].resource == MLC_RESOURCE_CRITICAL);
		assert(record->terminal_ledger[3].resource == MLC_RESOURCE_HEAP);
		assert(record->terminal_ledger[4].resource == MLC_RESOURCE_PAUSE);
		assert(record->released_resources == 5);
		assert(record->discarded_rows == 3 && !record->verdict_allowed);
		assert(mlc_lifecycle_begin(&lifecycle) == 0);
		mlc_lifecycle_complete(&lifecycle);
		assert(mlc_lifecycle_record(&lifecycle)->valid);
	}
}

static void test_phase_order_and_illegal_transitions(void)
{
	struct mlc_lifecycle_s lifecycle;
	struct cleanup_trace_s trace = { { 0 }, 0 };
	struct cleanup_arg_s args[] = {
		{ &trace, 1 }, { &trace, 2 }, { &trace, 3 }, { &trace, 4 }
	};

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_DOMAIN,
		MLC_RESOURCE_DOMAIN, record_cleanup, &args[0]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_CRITICAL,
		MLC_RESOURCE_CRITICAL, record_cleanup, &args[1]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_HEAP, record_cleanup, &args[2]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_HEAP, record_cleanup, &args[3]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_DOMAIN,
		MLC_RESOURCE_DOMAIN, record_cleanup, &args[0]) == -EINVAL);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_CRITICAL, record_cleanup, &args[0]) == -EINVAL);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_CRITICAL) == -EINVAL);
	mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CONTENTION);
	assert(trace.count == 4);
	assert(trace.order[0] == 4 && trace.order[1] == 3);
	assert(trace.order[2] == 2 && trace.order[3] == 1);
	puts("MLC_TASK5_PHASE_ORDER status=PASS acquire=domain,critical,heaps unwind=heaps,critical,domain");
}

static void test_every_missing_predecessor_and_phase_skip(void)
{
	struct mlc_lifecycle_s lifecycle;
	struct cleanup_trace_s trace = { { 0 }, 0 };
	struct cleanup_arg_s args[] = {
		{ &trace, 1 }, { &trace, 2 }, { &trace, 3 }, { &trace, 4 }
	};

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_CRITICAL,
		MLC_RESOURCE_CRITICAL, record_cleanup, &args[0]) == -EINVAL);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_HEAP, record_cleanup, &args[0]) == -EINVAL);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_CAPTURED) == -EINVAL);
	mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CONTENTION);

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_WORKSPACE,
		MLC_RESOURCE_WORKSPACE, record_cleanup, &args[0]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_CRITICAL,
		MLC_RESOURCE_CRITICAL, record_cleanup, &args[1]) == -EINVAL);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_HEAP, record_cleanup, &args[1]) == -EINVAL);
	mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CONTENTION);

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_DOMAIN,
		MLC_RESOURCE_DOMAIN, record_cleanup, &args[0]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_HEAP, record_cleanup, &args[1]) == -EINVAL);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_ANALYSIS) == -EINVAL);
	mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CONTENTION);

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_DOMAIN) == -EINVAL);
	mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CONTENTION);

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_DOMAIN,
		MLC_RESOURCE_DOMAIN, record_cleanup, &args[0]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_CRITICAL,
		MLC_RESOURCE_CRITICAL, record_cleanup, &args[1]) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_CAPTURED) == -EINVAL);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_HEAP, record_cleanup, &args[2]) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_ANALYSIS) == -EINVAL);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_COPIED) == -EINVAL);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_CAPTURED) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_PAUSED,
		MLC_RESOURCE_PAUSE, record_cleanup, &args[3]) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_COPIED) == -EINVAL);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_ANALYSIS) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_COPIED) == 0);
	mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CONTENTION);
	puts("MLC_TASK5_PHASE_PREDECESSORS status=PASS missing=critical,heaps,domain_resource skips=domain_heaps,captured,analysis,copied");
}

struct report_row_s {
	unsigned int heap;
	unsigned int value;
};

static void test_atomic_provisional_report(void)
{
	struct mlc_lifecycle_s lifecycle;
	struct report_row_s rows[2] = { { 0, 0 }, { 0, 0 } };
	struct report_row_s first = { 1, 11 };
	struct report_row_s second = { 2, 22 };
	struct cleanup_trace_s success_trace = { { 0 }, 0 };
	struct cleanup_arg_s success_resources[] = {
		{ &success_trace, 1 }, { &success_trace, 2 },
		{ &success_trace, 3 }, { &success_trace, 4 }
	};
	const struct mlc_post_release_record_s *record;

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_bind_report(&lifecycle, rows, 2,
		sizeof(rows[0])) == 0);
	assert(mlc_lifecycle_store_provisional(&lifecycle, &first) == 0);
	mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_ROOTS);
	record = mlc_lifecycle_record(&lifecycle);
	assert(record != NULL && record->discarded_rows == 1);
	assert(rows[0].heap == 0 && rows[0].value == 0);
	assert(lifecycle.report.count == 0 && !lifecycle.report.sealed);

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_bind_report(&lifecycle, rows, 1,
		sizeof(rows[0])) == 0);
	assert(mlc_lifecycle_store_provisional(&lifecycle, &first) == 0);
	assert(mlc_lifecycle_store_provisional(&lifecycle, &second) == -ENOSPC);
	mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CAPACITY);
	assert(lifecycle.report.count == 0 && rows[0].heap == 0);
	assert(mlc_lifecycle_record(&lifecycle)->discarded_rows == 1);

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_bind_report(&lifecycle, rows, 2,
		sizeof(rows[0])) == 0);
	assert(mlc_lifecycle_store_provisional(&lifecycle, &first) == 0);
	assert(mlc_lifecycle_store_provisional(&lifecycle, &second) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_DOMAIN,
		MLC_RESOURCE_DOMAIN, record_cleanup, &success_resources[0]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_CRITICAL,
		MLC_RESOURCE_CRITICAL, record_cleanup, &success_resources[1]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_HEAP, record_cleanup, &success_resources[2]) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_CAPTURED) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_PAUSED,
		MLC_RESOURCE_PAUSE, record_cleanup, &success_resources[3]) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_ANALYSIS) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_COPIED) == 0);
	mlc_lifecycle_complete(&lifecycle);
	assert(lifecycle.report.sealed && lifecycle.report.count == 2);
	assert(rows[0].heap == 1 && rows[1].heap == 2);
	assert(mlc_lifecycle_record(&lifecycle)->terminal_phase == MLC_PHASE_COPIED);
	assert(mlc_lifecycle_record(&lifecycle)->terminal_resources == 4);
	assert(mlc_lifecycle_record(&lifecycle)->released_resources == 4);
	puts("MLC_TASK5_REPORT_ATOMIC status=PASS two_heap_late_failure=true capacity_no_partial=true");
}

struct admission_race_s {
	struct mlc_lifecycle_s lifecycle;
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	bool cleanup_entered;
	bool allow_cleanup;
	bool workspace_live;
};

static void blocking_workspace_cleanup(void *arg)
{
	struct admission_race_s *race = arg;

	assert(pthread_mutex_lock(&race->mutex) == 0);
	race->cleanup_entered = true;
	assert(pthread_cond_signal(&race->condition) == 0);
	while (!race->allow_cleanup) {
		assert(pthread_cond_wait(&race->condition, &race->mutex) == 0);
	}
	race->workspace_live = false;
	assert(pthread_mutex_unlock(&race->mutex) == 0);
}

static void *admission_owner(void *arg)
{
	struct admission_race_s *race = arg;

	assert(mlc_lifecycle_begin(&race->lifecycle) == 0);
	assert(mlc_lifecycle_push(&race->lifecycle, MLC_PHASE_WORKSPACE,
			MLC_RESOURCE_WORKSPACE, blocking_workspace_cleanup, race) == 0);
	mlc_lifecycle_complete(&race->lifecycle);
	return NULL;
}

static void test_admission_until_workspace_destroyed(void)
{
	unsigned int repeat;

	for (repeat = 0; repeat < 100; repeat++) {
		struct admission_race_s race;
		struct mlc_lifecycle_s contender;
		pthread_t thread;

		race.cleanup_entered = false;
		race.allow_cleanup = false;
		race.workspace_live = true;
		assert(pthread_mutex_init(&race.mutex, NULL) == 0);
		assert(pthread_cond_init(&race.condition, NULL) == 0);
		assert(pthread_create(&thread, NULL, admission_owner, &race) == 0);
		assert(pthread_mutex_lock(&race.mutex) == 0);
		while (!race.cleanup_entered) {
			assert(pthread_cond_wait(&race.condition, &race.mutex) == 0);
		}
		assert(race.workspace_live);
		assert(mlc_lifecycle_begin(&contender) == -EBUSY);
		assert(race.workspace_live);
		race.allow_cleanup = true;
		assert(pthread_cond_signal(&race.condition) == 0);
		assert(pthread_mutex_unlock(&race.mutex) == 0);
		assert(pthread_join(thread, NULL) == 0);
		assert(!race.workspace_live);
		assert(mlc_lifecycle_begin(&contender) == 0);
		mlc_lifecycle_complete(&contender);
		assert(mlc_lifecycle_record(&race.lifecycle)->released_resources == 1);
		assert(pthread_cond_destroy(&race.condition) == 0);
		assert(pthread_mutex_destroy(&race.mutex) == 0);
	}
}

static void test_budget_boundaries(void)
{
	struct mlc_budget_s budget;
	uint64_t now = 100;

	assert(mlc_budget_start(&budget, fake_clock, &now, 2, 78, 80) == 0);
	assert(mlc_budget_consume(&budget, 2) == 0);
	assert(budget.operations_left == 0);
	assert(mlc_budget_consume(&budget, 1) == -E2BIG);

	now = 200;
	assert(mlc_budget_start(&budget, fake_clock, &now, 5, 78, 80) == 0);
	now = 278;
	assert(mlc_budget_consume(&budget, 1) == -ETIME);
	now = 280;
	assert(mlc_budget_request_resume(&budget) == 0);
	now = 281;
	assert(mlc_budget_request_resume(&budget) == -ETIME);
	assert(budget.state == MLC_BUDGET_FATAL);
}

static void test_isolated_fatal_stub_keeps_owned_resources(void)
{
	struct mlc_lifecycle_s lifecycle;
	struct cleanup_trace_s trace = { { 0 }, 0 };
	struct cleanup_arg_s resources[] = {
		{ &trace, 1 }, { &trace, 2 }, { &trace, 3 }, { &trace, 4 }
	};
	bool called = false;

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_DOMAIN,
		MLC_RESOURCE_DOMAIN, record_cleanup, &resources[0]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_CRITICAL,
		MLC_RESOURCE_CRITICAL, record_cleanup, &resources[1]) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_HEAP, record_cleanup, &resources[2]) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_CAPTURED) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_PAUSED,
			MLC_RESOURCE_PAUSE, record_cleanup, &resources[3]) == 0);
	mlc_lifecycle_add_provisional_row(&lifecycle);
	mlc_lifecycle_invoke_fatal(&lifecycle, fatal_stub, &called);
	assert(called && lifecycle.admitted && lifecycle.count == 4);
	assert(trace.count == 0 && lifecycle.provisional_rows == 0);
	assert(!lifecycle.verdict_allowed);
	assert(mlc_lifecycle_record(&lifecycle) != NULL);
	assert(mlc_lifecycle_record(&lifecycle)->reason == MLC_FATAL_RESUME_AMBIGUOUS);
	assert(mlc_lifecycle_record(&lifecycle)->terminal_phase == MLC_PHASE_PAUSED);
	assert(mlc_lifecycle_record(&lifecycle)->terminal_resources == 4);
	assert(mlc_lifecycle_record(&lifecycle)->released_resources == 0);
}

int main(void)
{
	test_all_recoverable_failures_and_record_lifetime();
	puts("MLC_TASK5_LIFECYCLE_FAILURES status=PASS classes=5 phases=8");
	test_phase_order_and_illegal_transitions();
	test_every_missing_predecessor_and_phase_skip();
	test_atomic_provisional_report();
	test_admission_until_workspace_destroyed();
	puts("MLC_TASK5_ADMISSION_RACE status=PASS repeat=100");
	test_budget_boundaries();
	test_isolated_fatal_stub_keeps_owned_resources();
	puts("MLC_TASK5_FATAL_ISOLATED status=PASS ownership_retained=true");
	puts("MLC_TASK5_LIFECYCLE status=PASS recoverable_reuse=true post_release_record=true");
	return 0;
}
