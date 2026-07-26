#ifndef __TASK4_EXEC_TINYARA_ARCH_H
#define __TASK4_EXEC_TINYARA_ARCH_H

#include <stddef.h>

struct tcb_s;
typedef int (*main_t)(int argc, char **argv);

int up_create_stack(struct tcb_s *tcb, size_t stack_size, int task_type);

#endif
