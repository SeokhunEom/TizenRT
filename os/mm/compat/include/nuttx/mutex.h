/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_NUTTX_MM_MUTEX_H
#define TIZENRT_NUTTX_MM_MUTEX_H
#include <sys/types.h>
#include <stdbool.h>
#ifdef TRMM_HOST_TEST
#include <pthread.h>
typedef struct trmm_mutex_s {
  pthread_mutex_t native;
  pthread_t owner;
  unsigned depth;
} mutex_t;
#else
#include <semaphore.h>
typedef struct trmm_mutex_s {
  sem_t sem;
  pid_t owner;
  unsigned depth;
  int cancel_state;
} mutex_t;
#endif
int trmm_mutex_init(mutex_t *mutex);
int trmm_mutex_destroy(mutex_t *mutex);
int trmm_mutex_lock(mutex_t *mutex);
int trmm_mutex_trylock(mutex_t *mutex);
int trmm_mutex_unlock(mutex_t *mutex);
bool trmm_mutex_is_locked(mutex_t *mutex);
#define nxmutex_init trmm_mutex_init
#define nxmutex_destroy trmm_mutex_destroy
#define nxmutex_lock trmm_mutex_lock
#define nxmutex_trylock trmm_mutex_trylock
#define nxmutex_unlock trmm_mutex_unlock
#define nxmutex_is_locked trmm_mutex_is_locked
#endif
