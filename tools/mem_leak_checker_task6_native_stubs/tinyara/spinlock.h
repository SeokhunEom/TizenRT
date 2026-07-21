#ifndef __MLC_TASK6_NATIVE_SPINLOCK_H
#define __MLC_TASK6_NATIVE_SPINLOCK_H

#include <stdatomic.h>
#include <tinyara/irq.h>

typedef atomic_int spinlock_t;
typedef unsigned int cpu_set_t;

#define SP_UNLOCKED 0
#define SP_LOCKED 1
#define SP_DMB() atomic_thread_fence(memory_order_acquire)
#define SP_DSB() atomic_thread_fence(memory_order_release)
#define SP_SEV() ((void)0)

int spin_trylock_wo_note(volatile spinlock_t *lock);
void spin_unlock_wo_note(volatile spinlock_t *lock);
void spin_setbit(volatile cpu_set_t *set, unsigned int cpu,
		volatile spinlock_t *setlock, volatile spinlock_t *orlock);
void spin_clrbit(volatile cpu_set_t *set, unsigned int cpu,
		volatile spinlock_t *setlock, volatile spinlock_t *orlock);
irqstate_t spin_lock_irqsave(spinlock_t *lock);
void spin_unlock_irqrestore(spinlock_t *lock, irqstate_t flags);

#define spin_islocked(lock) \
	(atomic_load((spinlock_t *)(lock)) == SP_LOCKED)

#endif
