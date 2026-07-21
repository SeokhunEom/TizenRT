#ifndef __MLC_TASK6_NATIVE_SYSDBG_H
#define __MLC_TASK6_NATIVE_SYSDBG_H

#include <semaphore.h>

typedef enum {
	SEM_INIT = 0,
	SEM_ACQUIRE,
	SEM_RELEASE,
	SEM_WAITING,
	SEM_DESTROY
} sem_status_t;

void save_semaphore_history(sem_t *sem, void *addr, sem_status_t status);

#endif
