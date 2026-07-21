#ifndef __MLC_TASK6_NATIVE_SCHED_NOTE_H
#define __MLC_TASK6_NATIVE_SCHED_NOTE_H

#include <stdbool.h>

struct tcb_s;
void sched_note_csection(struct tcb_s *tcb, bool enter);

#endif
