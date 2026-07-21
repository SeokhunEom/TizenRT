#ifndef __SCHED_SCHED_H
#define __SCHED_SCHED_H

#include <stdbool.h>
#include <stdint.h>

struct tcb_s {
	int16_t irqcount;
};

struct dq_queue_s {
	void *head;
};

extern struct dq_queue_s g_pendingtasks;

int this_cpu(void);
struct tcb_s *this_task(void);
struct tcb_s *current_task(int cpu);
bool sched_islocked_global(void);
void up_release_pending(void);

#endif
