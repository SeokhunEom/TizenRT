#ifndef __MLC_TASK6_NATIVE_SUPPORT_H
#define __MLC_TASK6_NATIVE_SUPPORT_H

#include <stddef.h>
#include <tinyara/debug/sysdbg.h>
#include <tinyara/irq.h>
#include "sched/sched.h"

enum mlc_native_phase_e {
	MLC_NATIVE_PHASE_NONE = 0,
	MLC_NATIVE_PHASE_ACQUIRE,
	MLC_NATIVE_PHASE_RELEASE,
	MLC_NATIVE_PHASE_COUNT
};

extern unsigned int g_native_irq_depth;
extern unsigned int g_native_irq_max;
extern unsigned int g_native_phase_spin[MLC_NATIVE_PHASE_COUNT];
extern unsigned int g_native_pause_probes;
extern unsigned int g_native_sem_wait_calls;
extern unsigned int g_native_sched_locks;
extern unsigned int g_native_sched_unlocks;
extern sem_status_t g_native_history[4];
extern unsigned int g_native_history_depth[4];
extern size_t g_native_history_count;

void mlc_native_reset(void);
void mlc_native_set_phase(enum mlc_native_phase_e phase);
struct tcb_s *mlc_native_tcb(void);
irqstate_t mlc_native_irq_state(void);

#endif
