#include <tinyara/config.h>

#ifdef CONFIG_TESTING

#include <stddef.h>
#include <stdint.h>

#include "mem_leak_checker_lifecycle.h"
#include "mem_leak_checker_roots.h"

static void mlc_test_release_resource(void *arg)
{
	size_t *released = arg;

	(*released)++;
}

int mlc_test_run_saved_task_scan(const struct tcb_s *tcb,
		enum mlc_saved_context_mode_e mode, uint32_t expected_cpu,
		struct mlc_test_saved_scan_result_s *result)
{
	struct mlc_saved_task_roots_s roots;
	struct mlc_lifecycle_s lifecycle;
	const struct mlc_post_release_record_s *record;
	uint32_t provisional_row = 0xfeedfaceu;
	uint32_t rows[2] = { 0, 0 };
	size_t released = 0;
	bool valid;

	if (result == NULL || mlc_lifecycle_begin(&lifecycle) < 0) {
		return -1;
	}
	if (mlc_lifecycle_push(&lifecycle, MLC_PHASE_WORKSPACE,
		MLC_RESOURCE_WORKSPACE, mlc_test_release_resource, &released) < 0 ||
		mlc_lifecycle_bind_report(&lifecycle, rows, 2, sizeof(rows[0])) < 0 ||
		mlc_lifecycle_store_provisional(&lifecycle, &provisional_row) < 0) {
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_CAPACITY);
		return -1;
	}

	valid = mlc_validate_saved_task_roots(tcb, mode, expected_cpu, &roots);
	if (valid) {
		mlc_lifecycle_complete(&lifecycle);
	} else {
		mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_TASK_CONTEXT);
	}
	record = mlc_lifecycle_record(&lifecycle);
	if (record == NULL) {
		return -1;
	}
	if (record->released_resources != released) {
		return -1;
	}

	result->reason = record->reason;
	result->published_rows = lifecycle.report.sealed ?
		lifecycle.report.count : 0;
	result->discarded_rows = record->discarded_rows;
	result->released_resources = record->released_resources;
	result->admitted = lifecycle.admitted;
	result->verdict_allowed = record->verdict_allowed;
	return valid ? 0 : -1;
}

#endif
