#ifndef __TASK4_EXEC_SCHED_SCHED_H
#define __TASK4_EXEC_SCHED_SCHED_H

#include <stdint.h>
#include <tinyara/sched.h>

int sched_releasetcb(struct tcb_s *tcb, uint8_t task_type);

#endif
