#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <tinyara/init.h>
#include <tinyara/irq.h>
#include <tinyara/sched_note.h>
#include <tinyara/spinlock.h>

#include "sched/sched.h"

irqstate_t g_fake_irq_state;
bool g_fake_interrupt_context;
void (*g_fake_irqsave_hook)(void);
int g_os_initstate;
struct dq_queue_s g_pendingtasks;

static struct tcb_s g_tcbs[2];
static int g_fake_cpu;
static unsigned int g_spin_notes[16];
static volatile spinlock_t *g_spin_note_locks[16];
static unsigned int g_spin_note_count;
static unsigned int g_csection_enter;
static unsigned int g_csection_leave;
static struct tcb_s *g_last_note_owner;

#ifdef CONFIG_SMP
extern volatile spinlock_t g_cpu_irqlock;
extern volatile spinlock_t g_cpu_irqsetlock;
extern volatile cpu_set_t g_cpu_irqset;
#endif

int irq_try_enter_critical_fresh(irqstate_t *flags);
void leave_critical_section(irqstate_t flags);

int this_cpu(void)
{
	return g_fake_cpu;
}

struct tcb_s *this_task(void)
{
	return &g_tcbs[g_fake_cpu];
}

struct tcb_s *current_task(int cpu)
{
	assert(cpu >= 0 && cpu < 2);
	return &g_tcbs[cpu];
}

bool sched_islocked_global(void)
{
	return false;
}

void up_release_pending(void)
{
}

bool up_cpu_pausereq(int cpu)
{
	(void)cpu;
	return false;
}

bool up_cpu_hotplugreq(int cpu)
{
	(void)cpu;
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

bool up_cpu_hotplugabort(int cpu)
{
	(void)cpu;
	return true;
}

void sched_note_csection(struct tcb_s *tcb, bool enter)
{
	assert(tcb == &g_tcbs[g_fake_cpu]);
	g_last_note_owner = tcb;
	if (enter) {
		g_csection_enter++;
	} else {
		g_csection_leave++;
	}
}

static void record_spin_note(struct tcb_s *tcb, volatile spinlock_t *lock,
		unsigned int type)
{
	assert(g_spin_note_count < 16);
	g_last_note_owner = tcb;
	g_spin_note_locks[g_spin_note_count] = lock;
	g_spin_notes[g_spin_note_count++] = type;
}

void sched_note_spinlock(struct tcb_s *tcb, volatile spinlock_t *lock,
		int type)
{
	record_spin_note(tcb, lock, (unsigned int)type);
}

int spin_trylock_wo_note(volatile spinlock_t *lock)
{
	return atomic_exchange((spinlock_t *)lock, SP_LOCKED);
}

void spin_unlock_wo_note(volatile spinlock_t *lock)
{
	atomic_store((spinlock_t *)lock, SP_UNLOCKED);
}

void spin_setbit(volatile cpu_set_t *set, unsigned int cpu,
		volatile spinlock_t *setlock, volatile spinlock_t *orlock)
{
	cpu_set_t previous;

	assert(spin_trylock_wo_note(setlock) == SP_UNLOCKED);
	record_spin_note(&g_tcbs[g_fake_cpu], setlock, NOTE_SPINLOCK_LOCK);
	record_spin_note(&g_tcbs[g_fake_cpu], setlock, NOTE_SPINLOCK_LOCKED);
	previous = *set;
	*set |= 1u << cpu;
	atomic_store((spinlock_t *)orlock, SP_LOCKED);
	if (previous == 0) {
		record_spin_note(&g_tcbs[g_fake_cpu], orlock, NOTE_SPINLOCK_LOCKED);
	}
	record_spin_note(&g_tcbs[g_fake_cpu], setlock, NOTE_SPINLOCK_UNLOCK);
	spin_unlock_wo_note(setlock);
}

void spin_clrbit(volatile cpu_set_t *set, unsigned int cpu,
		volatile spinlock_t *setlock, volatile spinlock_t *orlock)
{
	cpu_set_t previous;

	assert(spin_trylock_wo_note(setlock) == SP_UNLOCKED);
	record_spin_note(&g_tcbs[g_fake_cpu], setlock, NOTE_SPINLOCK_LOCK);
	record_spin_note(&g_tcbs[g_fake_cpu], setlock, NOTE_SPINLOCK_LOCKED);
	previous = *set;
	*set &= ~(1u << cpu);
	atomic_store((spinlock_t *)orlock, *set != 0 ? SP_LOCKED : SP_UNLOCKED);
	if (previous != 0 && *set == 0) {
		record_spin_note(&g_tcbs[g_fake_cpu], orlock, NOTE_SPINLOCK_UNLOCK);
	}
	record_spin_note(&g_tcbs[g_fake_cpu], setlock, NOTE_SPINLOCK_UNLOCK);
	spin_unlock_wo_note(setlock);
}

static void reset_state(void)
{
	g_fake_irq_state = 0;
	g_fake_interrupt_context = false;
	g_fake_irqsave_hook = NULL;
	g_fake_cpu = 0;
	g_os_initstate = OSINIT_OSREADY;
	g_tcbs[0].irqcount = 0;
	g_tcbs[1].irqcount = 0;
	g_pendingtasks.head = NULL;
	g_spin_note_count = 0;
	g_csection_enter = 0;
	g_csection_leave = 0;
	g_last_note_owner = NULL;
#ifdef CONFIG_SMP
	atomic_store((spinlock_t *)&g_cpu_irqlock, SP_UNLOCKED);
	atomic_store((spinlock_t *)&g_cpu_irqsetlock, SP_UNLOCKED);
	g_cpu_irqset = 0;
#endif
}

static void test_rejections(void)
{
	irqstate_t flags = 0x55;

	reset_state();
	assert(irq_try_enter_critical_fresh(NULL) == -EINVAL);
	g_fake_interrupt_context = true;
	assert(irq_try_enter_critical_fresh(&flags) == -EPERM);
	g_fake_interrupt_context = false;
	g_os_initstate = 0;
	assert(irq_try_enter_critical_fresh(&flags) == -EPERM);
	g_os_initstate = OSINIT_OSREADY;
	g_fake_irq_state = 1;
	assert(irq_try_enter_critical_fresh(&flags) == -EALREADY);
	assert(g_fake_irq_state == 1 && flags == 0x55);
	g_fake_irq_state = 0;
	g_tcbs[0].irqcount = 1;
	assert(irq_try_enter_critical_fresh(&flags) == -EALREADY);
	assert(g_spin_note_count == 0 && g_csection_enter == 0);
}

#ifdef CONFIG_SMP
static void test_smp_busy_rollback(void)
{
	irqstate_t flags = 0x55;

	reset_state();
	atomic_store((spinlock_t *)&g_cpu_irqlock, SP_LOCKED);
	assert(irq_try_enter_critical_fresh(&flags) == -EBUSY);
	assert(g_fake_irq_state == 0 && flags == 0x55);
	assert(g_cpu_irqset == 0 && g_tcbs[0].irqcount == 0);
	assert(g_spin_note_count == 0 && g_csection_enter == 0);

	reset_state();
	atomic_store((spinlock_t *)&g_cpu_irqsetlock, SP_LOCKED);
	assert(irq_try_enter_critical_fresh(&flags) == -EBUSY);
	assert(!spin_islocked(&g_cpu_irqlock));
	assert(spin_islocked(&g_cpu_irqsetlock));
	assert(g_fake_irq_state == 0 && flags == 0x55);
	assert(g_cpu_irqset == 0 && g_tcbs[0].irqcount == 0);
	assert(g_spin_note_count == 0 && g_csection_enter == 0);
}

static void assert_smp_enter_notes(void)
{
	unsigned int expected[] = {
		NOTE_SPINLOCK_LOCK,
		NOTE_SPINLOCK_LOCKED,
		NOTE_SPINLOCK_LOCK,
		NOTE_SPINLOCK_LOCKED,
		NOTE_SPINLOCK_LOCKED,
		NOTE_SPINLOCK_UNLOCK
	};
	unsigned int i;

	assert(g_spin_note_count == sizeof(expected) / sizeof(expected[0]));
	for (i = 0; i < g_spin_note_count; i++) {
		assert(g_spin_notes[i] == expected[i]);
	}
	assert(g_spin_note_locks[0] == &g_cpu_irqlock);
	assert(g_spin_note_locks[2] == &g_cpu_irqsetlock);
	assert(g_last_note_owner == &g_tcbs[g_fake_cpu]);
}

static void migrate_during_irqsave(void)
{
	g_fake_cpu = 1;
	g_fake_irqsave_hook = NULL;
}

static void test_migration_boundary_identity(void)
{
	irqstate_t flags = 0x55;

	reset_state();
	g_fake_irqsave_hook = migrate_during_irqsave;
	assert(irq_try_enter_critical_fresh(&flags) == 0);
	assert(flags == 0 && g_fake_cpu == 1);
	assert(g_tcbs[0].irqcount == 0 && g_tcbs[1].irqcount == 1);
	assert(g_cpu_irqset == (1u << 1));
	leave_critical_section(flags);
	assert(g_tcbs[0].irqcount == 0 && g_tcbs[1].irqcount == 0);
	assert(g_cpu_irqset == 0 && !spin_islocked(&g_cpu_irqlock));
	puts("MLC_TASK5_MIGRATION_BOUNDARY status=PASS published_cpu=1 stale_cpu=0");
}
#endif

static void test_success_and_ordinary_leave(void)
{
	irqstate_t flags = 0x55;
	unsigned int repeat;

	for (repeat = 0; repeat < 100; repeat++) {
		reset_state();
		assert(irq_try_enter_critical_fresh(&flags) == 0);
		assert(flags == 0 && g_fake_irq_state == 1 && g_tcbs[0].irqcount == 1);
		assert(g_csection_enter == 1 && g_csection_leave == 0);
		assert(g_last_note_owner == &g_tcbs[0]);
#ifdef CONFIG_SMP
		assert(spin_islocked(&g_cpu_irqlock));
		assert(!spin_islocked(&g_cpu_irqsetlock));
		assert(g_cpu_irqset == 1);
		assert_smp_enter_notes();
#else
		assert(g_spin_note_count == 0);
#endif
		g_spin_note_count = 0;
		leave_critical_section(flags);
		assert(g_fake_irq_state == 0 && g_tcbs[0].irqcount == 0);
		assert(g_csection_enter == 1 && g_csection_leave == 1);
#ifdef CONFIG_SMP
		assert(!spin_islocked(&g_cpu_irqlock));
		assert(!spin_islocked(&g_cpu_irqsetlock));
		assert(g_cpu_irqset == 0);
		assert(g_spin_note_count == 4);
#else
		assert(g_spin_note_count == 0);
#endif
	}
}

int main(void)
{
	test_rejections();
#ifdef CONFIG_SMP
	test_smp_busy_rollback();
	test_migration_boundary_identity();
#endif
	test_success_and_ordinary_leave();
#ifdef CONFIG_SMP
	puts("MLC_TASK5_IRQ_ACTUAL variant=smp_irqcount status=PASS repeat=100");
#else
	puts("MLC_TASK5_IRQ_ACTUAL variant=up_irqcount status=PASS repeat=100");
#endif
	return 0;
}
