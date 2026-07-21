#ifndef __KERNEL_DEBUG_MEM_LEAK_CHECKER_PAUSE_OWNER_H
#define __KERNEL_DEBUG_MEM_LEAK_CHECKER_PAUSE_OWNER_H

#include <stdbool.h>
#include <stdint.h>

#include "mem_leak_checker_budget.h"

struct mlc_pause_owner_s {
	uint64_t epoch_usec;
	uint64_t last_usec;
	uint32_t token;
	uint32_t stagnant_polls;
	int remote_cpu;
	int error;
	bool requested;
	bool active;
	struct mlc_budget_counters_s budget;
	struct mlc_budget_counters_s *shared_budget;
};

int mlc_pause_owner_begin(struct mlc_pause_owner_s *owner,
		uint64_t epoch_usec);
int mlc_pause_owner_begin_with_budget(struct mlc_pause_owner_s *owner,
		uint64_t epoch_usec, struct mlc_budget_counters_s *budget);
bool mlc_pause_owner_work_allowed(struct mlc_pause_owner_s *owner);
void mlc_pause_owner_cleanup(void *arg);

#endif
