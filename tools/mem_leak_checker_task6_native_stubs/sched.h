#ifndef __MLC_TASK6_NATIVE_SCHED_H
#define __MLC_TASK6_NATIVE_SCHED_H

struct tcb_s;

void sched_lock(void);
void sched_unlock(void);
int sched_setpriority(struct tcb_s *tcb, int priority);

#endif
