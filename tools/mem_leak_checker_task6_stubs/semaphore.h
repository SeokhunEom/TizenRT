#ifndef __SEMAPHORE_H
#define __SEMAPHORE_H

#include <pthread.h>

typedef struct {
	pthread_mutex_t lock;
	pthread_cond_t condition;
	int semcount;
	unsigned int flags;
} sem_t;

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_post(sem_t *sem);

#endif
