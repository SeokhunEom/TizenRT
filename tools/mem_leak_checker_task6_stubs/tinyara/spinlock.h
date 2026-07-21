#ifndef __TINYARA_SPINLOCK_H
#define __TINYARA_SPINLOCK_H

#include <stdint.h>

typedef uint8_t spinlock_t;

#define SP_UNLOCKED ((spinlock_t)0)
#define SP_LOCKED ((spinlock_t)1)
#define SP_DMB() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define SP_DSB() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define SP_SEV() ((void)0)

spinlock_t up_testset(volatile spinlock_t *lock);

#endif
