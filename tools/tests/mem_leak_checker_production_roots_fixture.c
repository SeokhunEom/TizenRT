#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <tinyara/config.h>
#include <tinyara/clock.h>
#include <tinyara/sched.h>

#include "mem_leak_checker_lifecycle.h"
#include "mem_leak_checker_roots.h"

typedef int (*saved_scan_fn_t)(const struct tcb_s *tcb,
		enum mlc_saved_context_mode_e mode, uint32_t expected_cpu,
		struct mlc_test_saved_scan_result_s *result);

clock_t clock_systimer(void)
{
	return 1;
}

int mlc_test_run_saved_task_scan_a(const struct tcb_s *tcb,
		enum mlc_saved_context_mode_e mode, uint32_t expected_cpu,
		struct mlc_test_saved_scan_result_s *result);
int mlc_test_run_saved_task_scan_m(const struct tcb_s *tcb,
		enum mlc_saved_context_mode_e mode, uint32_t expected_cpu,
		struct mlc_test_saved_scan_result_s *result);

static void assert_scan(saved_scan_fn_t scan, struct tcb_s *tcb,
		enum mlc_saved_context_mode_e mode, uint32_t expected_cpu,
		bool expected_valid)
{
	struct mlc_test_saved_scan_result_s result;
	int ret;

	memset(&result, 0, sizeof(result));
	ret = scan(tcb, mode, expected_cpu, &result);
	assert((ret == 0) == expected_valid);
	assert(!result.admitted);
	assert(result.released_resources == 1);
	if (expected_valid) {
		assert(result.reason == MLC_INCOMPLETE_NONE);
		assert(result.published_rows == 1);
		assert(result.discarded_rows == 0);
		assert(result.verdict_allowed);
	} else {
		assert(result.reason == MLC_INCOMPLETE_TASK_CONTEXT);
		assert(result.published_rows == 0);
		assert(result.discarded_rows == 1);
		assert(!result.verdict_allowed);
	}
}

static void assert_rejected_then_valid(saved_scan_fn_t scan,
		struct tcb_s *invalid, struct tcb_s *valid)
{
	assert_scan(scan, invalid, MLC_CONTEXT_BLOCKED, 0, false);
	assert_scan(scan, valid, MLC_CONTEXT_BLOCKED, 0, true);
}

int main(int argc, char **argv)
{
	uint32_t registers_a[17] = { 0 };
	uint32_t registers_m[2] = { 0 };
	struct tcb_s valid_a = {
		.adj_stack_ptr = 0x20001000u,
		.adj_stack_size = 0x1000u,
		.task_state = 2,
		.cpu = 0,
		.xcp = { registers_a }
	};
	struct tcb_s valid_m = {
		.adj_stack_ptr = 0x20000ffcu,
		.adj_stack_size = 0x1000u,
		.task_state = 2,
		.cpu = 0,
		.xcp = { registers_m }
	};
	struct tcb_s invalid;
	uint32_t valid_a_statuses[] = { 0x10u, 0x13u | 0xc0u, 0x1fu | 0x80u };
	size_t index;
	bool happy;

	if (argc != 2) {
		return 64;
	}
	happy = strcmp(argv[1], "mlc_task_roots") == 0;
	if (!happy && strcmp(argv[1], "mlc_invalid_task_irq_context") != 0) {
		return 64;
	}

	registers_a[13] = 0x20000800u;
	registers_a[16] = 0x13u | 0xc0u;
	registers_m[0] = 0x20000800u;
	registers_m[1] = 1u << 24;

	if (happy) {
		for (index = 0; index < sizeof(valid_a_statuses) /
			sizeof(valid_a_statuses[0]); index++) {
			registers_a[16] = valid_a_statuses[index];
			assert_scan(mlc_test_run_saved_task_scan_a, &valid_a,
				MLC_CONTEXT_BLOCKED, 0, true);
		}
		assert_scan(mlc_test_run_saved_task_scan_m, &valid_m,
			MLC_CONTEXT_BLOCKED, 0, true);
		printf("MLC_TASK8_PRODUCTION_ROOTS status=PASS roots=saved_task "
			"cpsr=usr,svc,sys xpsr=thumb released=1\n");
		return 0;
	}
	registers_a[16] = 0x11u;
	assert_rejected_then_valid(mlc_test_run_saved_task_scan_a, &valid_a,
		&(struct tcb_s){
			.adj_stack_ptr = 0x20001000u,
			.adj_stack_size = 0x1000u, .task_state = 2,
			.xcp = { (uint32_t[17]){
				[13] = 0x20000800u, [16] = 0x13u
			} }
		});
	registers_a[16] = 0x12u;
	assert_rejected_then_valid(mlc_test_run_saved_task_scan_a, &valid_a,
		&(struct tcb_s){
			.adj_stack_ptr = 0x20001000u,
			.adj_stack_size = 0x1000u, .task_state = 2,
			.xcp = { (uint32_t[17]){
				[13] = 0x20000800u, [16] = 0x13u
			} }
		});
	registers_a[16] = 0x13u | 0x00100000u;
	assert_rejected_then_valid(mlc_test_run_saved_task_scan_a, &valid_a,
		&(struct tcb_s){
			.adj_stack_ptr = 0x20001000u,
			.adj_stack_size = 0x1000u, .task_state = 2,
			.xcp = { (uint32_t[17]){
				[13] = 0x20000800u, [16] = 0x13u
			} }
		});

	registers_m[1] = 0;
	assert_rejected_then_valid(mlc_test_run_saved_task_scan_m, &valid_m,
		&(struct tcb_s){
			.adj_stack_ptr = 0x20000ffcu,
			.adj_stack_size = 0x1000u, .task_state = 2,
			.xcp = { (uint32_t[2]){
				[0] = 0x20000800u, [1] = 1u << 24
			} }
		});
	registers_m[1] = (1u << 24) | 3u;
	assert_rejected_then_valid(mlc_test_run_saved_task_scan_m, &valid_m,
		&(struct tcb_s){
			.adj_stack_ptr = 0x20000ffcu,
			.adj_stack_size = 0x1000u, .task_state = 2,
			.xcp = { (uint32_t[2]){
				[0] = 0x20000800u, [1] = 1u << 24
			} }
		});

	invalid = valid_a;
	registers_a[16] = 0x13u;
	registers_a[13] = 0x20000801u;
	assert_rejected_then_valid(mlc_test_run_saved_task_scan_a, &invalid,
		&(struct tcb_s){
			.adj_stack_ptr = 0x20001000u,
			.adj_stack_size = 0x1000u, .task_state = 2,
			.xcp = { (uint32_t[17]){
				[13] = 0x20000800u, [16] = 0x13u
			} }
		});
	registers_a[13] = 0x20000800u;
	invalid = valid_a;
	invalid.task_state = TSTATE_TASK_RUNNING;
	invalid.cpu = 2;
	assert_scan(mlc_test_run_saved_task_scan_a, &invalid,
		MLC_CONTEXT_REMOTE_PAUSED, 1, false);
	assert_scan(mlc_test_run_saved_task_scan_a, &valid_a,
		MLC_CONTEXT_BLOCKED, 0, true);

	printf("MLC_TASK8_PRODUCTION_FAILURE status=PASS "
		"mutations=cpsr_fiq,cpsr_irq,cpsr_reserved,xpsr_thumb,xpsr_ipsr,sp,migration "
		"incomplete=TASK_CONTEXT rows=0 released=1 reuse=valid_after_rejection\n");
	return 0;
}
