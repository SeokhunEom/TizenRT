#ifndef __TINYARA_SCHED_NOTE_H
#define __TINYARA_SCHED_NOTE_H

#include <stdbool.h>
#include <tinyara/spinlock.h>

#define NOTE_SPINLOCK_LOCK 14
#define NOTE_SPINLOCK_LOCKED 15
#define NOTE_SPINLOCK_UNLOCK 16
#define NOTE_SPINLOCK_ABORT 17

struct tcb_s;

void sched_note_csection(struct tcb_s *tcb, bool enter);
void sched_note_spinlock(struct tcb_s *tcb, volatile spinlock_t *lock,
		int type);

#endif
