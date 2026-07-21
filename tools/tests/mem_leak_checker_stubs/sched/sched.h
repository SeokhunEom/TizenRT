#ifndef __TEST_SCHED_SCHED_H
#define __TEST_SCHED_SCHED_H

#include <tinyara/sched.h>

static inline struct tcb_s *current_task(int cpu)
{
	(void)cpu;
	return NULL;
}

#endif
