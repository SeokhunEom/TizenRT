#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "mem_leak_checker_report.h"

static void test_primary_states(void)
{
	struct mlc_report_summary_s summary = {
		.definite_count = 0,
		.ambiguous_count = 0,
		.broken_count = 0,
		.complete = true,
		.reason = MLC_INCOMPLETE_NONE
	};

	assert(mlc_report_primary(&summary) == MLC_REPORT_PRIMARY_CLEAN);
	summary.definite_count = 2;
	assert(mlc_report_primary(&summary) == MLC_REPORT_PRIMARY_DEFINITE);
	summary.definite_count = 0;
	summary.ambiguous_count = 3;
	assert(mlc_report_primary(&summary) == MLC_REPORT_PRIMARY_AMBIGUOUS_ONLY);
	summary.complete = false;
	summary.reason = MLC_INCOMPLETE_CAPACITY;
	assert(mlc_report_primary(&summary) == MLC_REPORT_PRIMARY_INCOMPLETE);
	summary.complete = true;
	summary.reason = MLC_INCOMPLETE_NONE;
	summary.broken_count = 1;
	assert(mlc_report_primary(&summary) == MLC_REPORT_PRIMARY_INCOMPLETE);
}

static void test_reason_precedence(void)
{
	static const enum mlc_incomplete_reason_e order[] = {
		MLC_INCOMPLETE_HEAP_CORRUPT,
		MLC_INCOMPLETE_DOMAIN_CHANGED,
		MLC_INCOMPLETE_TASK_CONTEXT,
		MLC_INCOMPLETE_PREOWNED_RESOURCE,
		MLC_INCOMPLETE_UNSUPPORTED_CONTEXT,
		MLC_INCOMPLETE_GENERATION_EXHAUSTED,
		MLC_INCOMPLETE_CLOCK,
		MLC_INCOMPLETE_CAPACITY,
		MLC_INCOMPLETE_BUDGET,
		MLC_INCOMPLETE_DEADLINE,
		MLC_INCOMPLETE_BUSY_REGISTRY,
		MLC_INCOMPLETE_BUSY_CRITICAL,
		MLC_INCOMPLETE_BUSY_HEAP,
		MLC_INCOMPLETE_INTERNAL
	};
	size_t index;

	for (index = 0; index < sizeof(order) / sizeof(order[0]); index++) {
		size_t lower;

		for (lower = index + 1; lower < sizeof(order) / sizeof(order[0]); lower++) {
			assert(mlc_report_select_reason(order[lower], order[index]) == order[index]);
		}
	}
	assert(mlc_report_select_reason(MLC_INCOMPLETE_NONE,
		MLC_INCOMPLETE_CLOCK) == MLC_INCOMPLETE_CLOCK);
	assert(mlc_report_reason_name(MLC_INCOMPLETE_HEAP_CORRUPT)[0] == 'H');
}

static void test_record_bounds(void)
{
	static const unsigned char data[32] = { 0 };
	struct mlc_report_record_s record = {
		.type = MLC_REPORT_RECORD_DEFINITE,
		.address = 0x1000,
		.capacity = 64,
		.requested_size = 33,
		.owner = 0x2000,
		.pid = 7,
		.dump = data,
		.dump_size = 32,
		.scc_id = 0,
		.provenance = 1
	};

	assert(mlc_report_record_validate(&record) == 0);
	assert(mlc_report_dump_limit(33) == 32);
	record.dump_size = 33;
	assert(mlc_report_record_validate(&record) < 0);
	record.dump_size = 0;
	record.requested_size = 65;
	assert(mlc_report_record_validate(&record) < 0);
	record.requested_size = 0;
	record.capacity = 0;
	record.type = MLC_REPORT_RECORD_BROKEN;
	assert(mlc_report_record_validate(&record) == 0);
	record.type = (enum mlc_report_record_type_e)-1;
	assert(mlc_report_record_validate(&record) < 0);
	assert(mlc_report_is_legacy_record(MLC_REPORT_RECORD_DEFINITE));
	assert(!mlc_report_is_legacy_record(MLC_REPORT_RECORD_DETAIL));
}

int main(void)
{
	test_primary_states();
	test_reason_precedence();
	test_record_bounds();
	puts("MLC_HOST fixture=mlc_report_contract status=PASS");
	puts("MLC_HOST fixture=mlc_reason_precedence status=PASS");
	puts("MLC_HOST fixture=mlc_legacy_row_bounds status=PASS");
	return 0;
}
