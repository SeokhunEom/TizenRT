#ifndef __TASK4_EXEC_TINYARA_SCHED_H
#define __TASK4_EXEC_TINYARA_SCHED_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <tinyara/config.h>

struct binary_s;

#define TCB_FLAG_TTYPE_TASK 0

struct tcb_s {
	pid_t pid;
	uint32_t *stack_alloc_ptr;
	uint32_t uheap;
	uint32_t uspace;
	uint32_t app_id;
	char name[CONFIG_TASK_NAME_SIZE + 1];
};

struct task_tcb_s {
	struct tcb_s cmn;
	struct binary_s *bininfo;
};

struct tcb_s *sched_self(void);
int task_init(struct tcb_s *tcb, const char *name, int priority, void *stack,
		size_t stack_size, int (*entry)(int, char **), char *const *argv);
int task_activate(struct tcb_s *tcb);

#endif
