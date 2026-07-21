#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <arch/irq.h>
#include <queue.h>
#include <sched.h>
#include <semaphore.h>
#include <tinyara/init.h>
#include <tinyara/mm/mm.h>
#include <tinyara/spinlock.h>

#include "mem_leak_checker_task6_native_support.h"

volatile dq_queue_t g_waitingforsemaphore;
struct dq_queue_s g_pendingtasks;
int g_os_initstate;

unsigned int g_native_irq_depth;
unsigned int g_native_irq_max;
unsigned int g_native_phase_spin[MLC_NATIVE_PHASE_COUNT];
unsigned int g_native_pause_probes;
unsigned int g_native_sem_wait_calls;
unsigned int g_native_sched_locks;
unsigned int g_native_sched_unlocks;
sem_status_t g_native_history[4];
unsigned int g_native_history_depth[4];
size_t g_native_history_count;

static struct tcb_s g_native_tcbs[2];
static irqstate_t g_native_irq_state;
static enum mlc_native_phase_e g_native_phase;

#ifdef CONFIG_SMP
extern volatile spinlock_t g_cpu_irqlock;
extern volatile spinlock_t g_cpu_irqsetlock;
extern volatile cpu_set_t g_cpu_irqset;
extern volatile uint8_t g_cpu_nestcount[CONFIG_SMP_NCPUS];
#endif

irqstate_t mlc_native_irqsave(void)
{
#ifdef MLC_TASK6_PRODUCTION_SUPPORT
	return 0;
#else
	irqstate_t saved = g_native_irq_state;

	g_native_irq_state = 1;
	g_native_irq_depth++;
	if (g_native_irq_depth > g_native_irq_max) {
		g_native_irq_max = g_native_irq_depth;
	}
	return saved;
#endif
}

void mlc_native_irqrestore(irqstate_t flags)
{
#ifdef MLC_TASK6_PRODUCTION_SUPPORT
	(void)flags;
#else
	assert(g_native_irq_depth > 0);
	g_native_irq_depth--;
	g_native_irq_state = flags;
#endif
}

bool mlc_native_interrupt_context(void)
{
	return false;
}

int this_cpu(void)
{
	return 0;
}

struct tcb_s *this_task(void)
{
	return &g_native_tcbs[0];
}

struct tcb_s *current_task(int cpu)
{
	assert(cpu >= 0 && cpu < 2);
	return &g_native_tcbs[cpu];
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
	(void)pshared;
	assert(value <= INT16_MAX);
	memset(sem, 0, sizeof(*sem));
	sem->semcount = (int16_t)value;
	sem->flags = FLAGS_INITIALIZED;
	return 0;
}

int sem_wait(sem_t *sem)
{
#ifdef MLC_TASK6_PRODUCTION_SUPPORT
	if (sem->semcount <= 0) {
		errno = EAGAIN;
		return -1;
	}
	sem->semcount--;
	return 0;
#else
	(void)sem;
	g_native_sem_wait_calls++;
	errno = EDEADLK;
	return -1;
#endif
}

#ifdef MLC_TASK6_PRODUCTION_SUPPORT
int sem_trywait(sem_t *sem)
{
	return sem_wait(sem);
}

int sem_post(sem_t *sem)
{
	sem->semcount++;
	return 0;
}
#endif

struct mm_heap_s *mm_get_heap(void *address)
{
	return address;
}

void save_semaphore_history(sem_t *sem, void *addr, sem_status_t status)
{
	assert(sem != NULL && addr == this_task());
	assert(g_native_history_count < 4);
	g_native_history[g_native_history_count] = status;
	g_native_history_depth[g_native_history_count] = g_native_irq_depth;
	g_native_history_count++;
}

void sched_lock(void)
{
	g_native_sched_locks++;
}

void sched_unlock(void)
{
	g_native_sched_unlocks++;
}

int sched_setpriority(struct tcb_s *tcb, int priority)
{
	tcb->sched_priority = priority;
	return 0;
}

void up_unblock_task(struct tcb_s *tcb)
{
	assert(tcb != NULL);
}

bool sched_islocked_global(void)
{
	return false;
}

void up_release_pending(void)
{
}

void sched_note_csection(struct tcb_s *tcb, bool enter)
{
	assert(tcb == this_task());
	(void)enter;
}

int spin_trylock_wo_note(volatile spinlock_t *lock)
{
	if (g_native_phase != MLC_NATIVE_PHASE_NONE) {
		g_native_phase_spin[g_native_phase]++;
	}
	return atomic_exchange((spinlock_t *)lock, SP_LOCKED);
}

void spin_unlock_wo_note(volatile spinlock_t *lock)
{
	atomic_store((spinlock_t *)lock, SP_UNLOCKED);
}

irqstate_t spin_lock_irqsave(spinlock_t *lock)
{
	irqstate_t flags = irqsave();

	while (atomic_exchange(lock, SP_LOCKED) == SP_LOCKED) {
	}
	return flags;
}

void spin_unlock_irqrestore(spinlock_t *lock, irqstate_t flags)
{
	atomic_store(lock, SP_UNLOCKED);
	irqrestore(flags);
}

void spin_setbit(volatile cpu_set_t *set, unsigned int cpu,
		volatile spinlock_t *setlock, volatile spinlock_t *orlock)
{
	assert(spin_trylock_wo_note(setlock) == SP_UNLOCKED);
	*set |= 1u << cpu;
	atomic_store((spinlock_t *)orlock, SP_LOCKED);
	spin_unlock_wo_note(setlock);
}

void spin_clrbit(volatile cpu_set_t *set, unsigned int cpu,
		volatile spinlock_t *setlock, volatile spinlock_t *orlock)
{
	assert(spin_trylock_wo_note(setlock) == SP_UNLOCKED);
	*set &= ~(1u << cpu);
	atomic_store((spinlock_t *)orlock, *set ? SP_LOCKED : SP_UNLOCKED);
	spin_unlock_wo_note(setlock);
}

bool up_cpu_pausereq(int cpu)
{
	(void)cpu;
	g_native_pause_probes++;
	return false;
}

void up_cpu_paused_save(void)
{
}

bool up_cpu_paused(int cpu)
{
	(void)cpu;
	return true;
}

void up_cpu_paused_restore(void)
{
}

void mlc_native_set_phase(enum mlc_native_phase_e phase)
{
	g_native_phase = phase;
}

struct tcb_s *mlc_native_tcb(void)
{
	return this_task();
}

irqstate_t mlc_native_irq_state(void)
{
	return g_native_irq_state;
}

void mlc_native_reset(void)
{
	memset(g_native_tcbs, 0, sizeof(g_native_tcbs));
	g_native_tcbs[0].sched_priority = 100;
	g_native_tcbs[0].base_priority = 100;
	g_waitingforsemaphore.head = NULL;
	g_pendingtasks.head = NULL;
	g_os_initstate = OSINIT_OSREADY;
	g_native_irq_state = 0;
	g_native_irq_depth = 0;
	g_native_irq_max = 0;
	memset(g_native_phase_spin, 0, sizeof(g_native_phase_spin));
	g_native_pause_probes = 0;
	g_native_sem_wait_calls = 0;
	g_native_sched_locks = 0;
	g_native_sched_unlocks = 0;
	g_native_history_count = 0;
	g_native_phase = MLC_NATIVE_PHASE_NONE;
#ifdef CONFIG_SMP
	atomic_store((spinlock_t *)&g_cpu_irqlock, SP_UNLOCKED);
	atomic_store((spinlock_t *)&g_cpu_irqsetlock, SP_UNLOCKED);
	g_cpu_irqset = 0;
	memset((void *)g_cpu_nestcount, 0, sizeof(g_cpu_nestcount));
#endif
}
