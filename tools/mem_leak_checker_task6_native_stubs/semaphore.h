#ifndef __MLC_TASK6_NATIVE_SEMAPHORE_H
#define __MLC_TASK6_NATIVE_SEMAPHORE_H

#include <stdint.h>
#include <stddef.h>
#include <tinyara/config.h>

#define SAVE_SEM_HOLDER 1
#define PRIOINHERIT_FLAGS_DISABLE (1 << 0)
#define FLAGS_INITIALIZED (1 << 1)
#define FLAGS_SIGSEM (1 << 2)
#define FLAGS_SEM_MUTEX (1 << 3)
#define SEM_VALUE_MAX INT16_MAX

struct tcb_s;
struct sem_s;

struct semholder_s {
	struct semholder_s *tlink;
	struct sem_s *sem;
	struct tcb_s *htcb;
	int16_t counts;
};

typedef struct sem_s {
	int16_t semcount;
	uint8_t flags;
	struct semholder_s holder;
} sem_t;

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_post(sem_t *sem);

#endif
