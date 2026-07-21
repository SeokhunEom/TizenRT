#ifndef __KERNEL_DEBUG_MEM_LEAK_CHECKER_ROOTS_H
#define __KERNEL_DEBUG_MEM_LEAK_CHECKER_ROOTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tinyara/arch.h>
#include <tinyara/sched.h>

enum mlc_saved_context_mode_e {
	MLC_CONTEXT_BLOCKED = 0,
	MLC_CONTEXT_REMOTE_PAUSED,
	MLC_CONTEXT_IRQ
};

struct mlc_saved_task_roots_s {
	const uint32_t *registers;
	size_t register_count;
	uintptr_t stack_live_start;
	uintptr_t stack_high;
};

bool mlc_validate_current_capture(
		const struct up_mem_leak_capture_s *capture);
bool mlc_validate_saved_task_roots(const struct tcb_s *tcb,
		enum mlc_saved_context_mode_e mode, uint32_t expected_cpu,
		struct mlc_saved_task_roots_s *roots);
void up_mem_leak_capture_identity(struct up_mem_leak_capture_s *capture);
int run_all_mem_leak_checker_with_capture(int checker_pid,
		const struct up_mem_leak_capture_s *capture);

#ifdef CONFIG_TESTING
struct mlc_test_saved_scan_result_s {
	uint32_t reason;
	size_t published_rows;
	size_t discarded_rows;
	size_t released_resources;
	bool admitted;
	bool verdict_allowed;
};

int mlc_test_run_saved_task_scan(const struct tcb_s *tcb,
		enum mlc_saved_context_mode_e mode, uint32_t expected_cpu,
		struct mlc_test_saved_scan_result_s *result);
#endif

#endif
