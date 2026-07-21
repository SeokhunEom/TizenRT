#ifndef __MLC_TASK6_NATIVE_SCHED_SCHED_H
#define __MLC_TASK6_NATIVE_SCHED_SCHED_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <queue.h>
#include <semaphore.h>

struct task_group_s {
	int tg_binidx;
};

struct tcb_s {
	struct tcb_s *flink;
	struct tcb_s *bin_flink;
	sem_t *waitsem;
	struct semholder_s *holdsem;
	int16_t irqcount;
	int sched_priority;
	int base_priority;
	int boost_priority;
	uint32_t uheap;
	uint32_t uspace;
	uint32_t app_id;
	void *stack_alloc_ptr;
	void *adj_stack_ptr;
	size_t adj_stack_size;
	pid_t pid;
	char name[CONFIG_TASK_NAME_SIZE + 1];
	struct task_group_s *group;
};

struct task_tcb_s {
	struct tcb_s cmn;
	void *bininfo;
};

#define TCB_FLAG_TTYPE_TASK 1

extern volatile dq_queue_t g_waitingforsemaphore;
extern struct dq_queue_s g_pendingtasks;

int this_cpu(void);
struct tcb_s *this_task(void);
struct tcb_s *current_task(int cpu);
bool sched_islocked_global(void);
void up_release_pending(void);
int sched_setpriority(struct tcb_s *tcb, int priority);

#endif
