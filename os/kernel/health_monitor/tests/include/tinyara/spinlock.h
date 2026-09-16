/* SPDX-License-Identifier: Apache-2.0 */
/* Host stand-ins for local IRQ state and the dedicated SMP spinlock. */
#ifndef HEALTH_MONITOR_TEST_SPINLOCK_H
#define HEALTH_MONITOR_TEST_SPINLOCK_H
#include <stdint.h>
typedef unsigned int irqstate_t;
typedef uint8_t spinlock_t;
#define SP_UNLOCKED 0
#define SP_LOCKED 1
irqstate_t irqsave(void);
void irqrestore(irqstate_t flags);
void spin_lock_wo_note(volatile spinlock_t *lock);
void spin_unlock_wo_note(volatile spinlock_t *lock);
#endif
