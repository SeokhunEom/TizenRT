#ifndef __TEST_TINYARA_SCHED_H
#define __TEST_TINYARA_SCHED_H

#include <stddef.h>
#include <stdint.h>

#define TSTATE_TASK_RUNNING 1

struct xcptcontext_test_s {
	uint32_t *regs;
};

struct tcb_s {
	uint32_t adj_stack_ptr;
	size_t adj_stack_size;
	int task_state;
	uint32_t cpu;
	struct xcptcontext_test_s xcp;
};

#endif
