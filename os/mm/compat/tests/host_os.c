/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/mutex.h>
#include <nuttx/arch.h>
#include <errno.h>
#include <string.h>
_Thread_local bool trmm_test_irq;
_Thread_local bool trmm_test_switching;
static pthread_mutex_t irq_lock = PTHREAD_MUTEX_INITIALIZER;
int trmm_mutex_init(mutex_t *m)
{
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  m->depth = 0;
  int ret = pthread_mutex_init(&m->native, &attr);
  pthread_mutexattr_destroy(&attr);
  assert(ret == 0);
  return -ret;
}
int trmm_mutex_destroy(mutex_t *m)
{
  int ret = pthread_mutex_destroy(&m->native);
  assert(ret == 0);
  return -ret;
}
int trmm_mutex_lock(mutex_t *m)
{
  int ret = pthread_mutex_lock(&m->native);
  assert(ret == 0);
  m->owner = pthread_self();
  ++m->depth;
  return -ret;
}
int trmm_mutex_trylock(mutex_t *m)
{
  int ret = pthread_mutex_trylock(&m->native);
  if (ret == 0) { m->owner = pthread_self(); ++m->depth; }
  return -ret;
}
int trmm_mutex_unlock(mutex_t *m)
{
  assert(m->depth > 0 && pthread_equal(m->owner, pthread_self()));
  --m->depth;
  return -pthread_mutex_unlock(&m->native);
}
bool trmm_mutex_is_locked(mutex_t *m) { return m->depth != 0; }
bool trmm_interrupt_context(void) { return trmm_test_irq; }
irqstate_t trmm_irq_save(void) { pthread_mutex_lock(&irq_lock); return 0; }
void trmm_irq_restore(irqstate_t state) { (void)state; pthread_mutex_unlock(&irq_lock); }
pid_t trmm_gettid(void) { return trmm_test_switching ? -ESRCH : 1; }
