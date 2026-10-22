/* SPDX-License-Identifier: Apache-2.0 */
/* Temporary providers for NuttX mm's mutex, IRQ, and scheduler contracts.
 * Remove these providers when those NuttX modules replace TizenRT's APIs.
 * This file must NOT use compat/symbol_map.h. */
#include <tinyara/config.h>
#include <tinyara/compiler.h>
#include <tinyara/sched.h>
#include <tinyara/arch.h>
#include <tinyara/irq.h>
#include <sched.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <nuttx/mutex.h>

#ifdef CONFIG_SMP
#error "NuttX MM OS providers require an SMP implementation before use"
#endif

/* Never put an OS operation inside ASSERT: disabled assertions may omit it. */
static void trmm_require(bool condition)
{
  if (!condition) { PANIC(); for (;;) {} }
}

static int trmm_sem_result(int ret, int saved_errno)
{
  int result = ret < 0 ? -get_errno() : ret;
  set_errno(saved_errno);
  return result;
}

int trmm_mutex_init(mutex_t *mutex)
{
  int saved_errno = get_errno();
  int ret = sem_init(&mutex->sem, 0, 1);
  if (ret == 0)
    {
      mutex->sem.flags |= FLAGS_SEM_MUTEX;
      mutex->owner = -1;
      mutex->depth = 0;
      mutex->cancel_state = TASK_CANCEL_ENABLE;
    }
  return trmm_sem_result(ret, saved_errno);
}

int trmm_mutex_destroy(mutex_t *mutex)
{
  int saved_errno = get_errno();
  trmm_require(mutex->depth == 0);
  return trmm_sem_result(sem_destroy(&mutex->sem), saved_errno);
}

pid_t trmm_gettid(void)
{
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  struct tcb_s *tcb = sched_self();
  /* Before task lists exist, boot is the only execution context. During
   * task_exit, the ready head has already changed and is marked READYTORUN;
   * touching that task's stack/errno or waiting on a semaphore is unsafe. */
  if (tcb == NULL) { return 0; }
  return tcb->task_state == TSTATE_TASK_RUNNING ? tcb->pid : -ESRCH;
#else
  return getpid();
#endif
}

static bool trmm_have_task(void)
{
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  return sched_self() != NULL;
#else
  return true;
#endif
}

static int trmm_mutex_take(mutex_t *mutex, bool try_only)
{
  int saved_errno = get_errno();
  pid_t owner = trmm_gettid();
  int state = TASK_CANCEL_ENABLE;
  int ret;
  bool have_task = trmm_have_task();
  if (owner < 0) { return -ESRCH; }
  if (mutex->owner == owner && mutex->depth != 0)
    {
      trmm_require(mutex->depth != UINT_MAX);
      mutex->depth++;
      return 0;
    }
  /* Keep cancellation disabled throughout the protected allocator mutation.
   * Restoring it immediately after sem_wait could terminate a lock owner. */
  if (have_task) { trmm_require(task_setcancelstate(TASK_CANCEL_DISABLE, &state) == 0); }
  do
    {
      ret = try_only ? sem_trywait(&mutex->sem) : sem_wait(&mutex->sem);
    }
  while (ret < 0 && get_errno() == EINTR);
  if (ret == 0)
    {
      mutex->owner = owner;
      mutex->depth = 1;
      mutex->cancel_state = state;
    }
  else
    {
      int failure = get_errno();
      if (have_task) { trmm_require(task_setcancelstate(state, NULL) == 0); }
      set_errno(saved_errno);
      return -failure;
    }
  set_errno(saved_errno);
  return 0;
}

int trmm_mutex_lock(mutex_t *mutex)
{
  int ret = trmm_mutex_take(mutex, false);
  /* v12.13.0 mm_lock() discards nxmutex_lock's failure result. Do not let
   * the upstream caller continue with unlocked metadata on any hard error. */
  if (ret != 0) { PANIC(); for (;;) {} }
  return ret;
}

int trmm_mutex_trylock(mutex_t *mutex)
{
  return trmm_mutex_take(mutex, true);
}

int trmm_mutex_unlock(mutex_t *mutex)
{
  int saved_errno = get_errno();
  int state;
  trmm_require(mutex->owner == trmm_gettid() && mutex->depth > 0);
  if (--mutex->depth != 0) { return 0; }
  state = mutex->cancel_state;
  mutex->owner = -1;
  trmm_require(sem_post(&mutex->sem) == 0);
  if (trmm_have_task()) { trmm_require(task_setcancelstate(state, NULL) == 0); }
  set_errno(saved_errno);
  return 0;
}

bool trmm_mutex_is_locked(mutex_t *mutex)
{
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  irqstate_t flags = enter_critical_section();
#endif
  bool locked = mutex->sem.semcount <= 0;
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  leave_critical_section(flags);
#endif
  return locked;
}

bool trmm_interrupt_context(void)
{
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  return up_interrupt_context();
#else
  return false;
#endif
}

irqstate_t trmm_irq_save(void)
{
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  return enter_critical_section();
#else
  /* In protected userspace the upstream delayed-free list is compiled out.
   * There is no kernel data to protect and privileged IRQ instructions must
   * not execute. Heap serialization still uses the syscall-backed mutex. */
  return 0;
#endif
}

void trmm_irq_restore(irqstate_t state)
{
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  leave_critical_section(state);
#else
  (void)state;
#endif
}
