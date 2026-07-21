#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <arch/irq.h>
#include <tinyara/init.h>
#include <tinyara/spinlock.h>
#include "sched/sched.h"

extern irqstate_t enter_critical_section(void);
extern void leave_critical_section(irqstate_t flags);
extern volatile spinlock_t g_cpu_irqlock;
extern volatile cpu_set_t g_cpu_irqset;

int g_os_initstate = OSINIT_TASKLISTS;
struct readytorun_s g_pendingtasks;
static struct tcb_s g_tcb;
static irqstate_t g_irqsave_values[] = {11, 22, 33, 44};
static size_t g_irqsave_index;
static irqstate_t g_restored[8];
static size_t g_restored_count;
static bool g_pending;
static unsigned int g_poll_calls;

irqstate_t irqsave(void)
{
	assert(g_irqsave_index < sizeof(g_irqsave_values) /
		sizeof(g_irqsave_values[0]));
	return g_irqsave_values[g_irqsave_index++];
}

void irqrestore(irqstate_t flags)
{
	assert(g_restored_count < sizeof(g_restored) / sizeof(g_restored[0]));
	g_restored[g_restored_count++] = flags;
}

bool up_irq_saved_enabled(irqstate_t value)
{
	return value != 0;
}

bool up_interrupt_context(void)
{
	return false;
}

int this_cpu(void)
{
	return 0;
}

struct tcb_s *current_task(int cpu)
{
	assert(cpu == 0);
	return &g_tcb;
}

struct tcb_s *this_task(void)
{
	return &g_tcb;
}

void up_release_pending(void)
{
}

bool mlc_pause_poll_pending(int cpu, uintptr_t saved_flags)
{
	(void)saved_flags;
	assert(cpu == 0);
	return g_pending;
}

bool mlc_pause_service_poll(int cpu, uintptr_t saved_flags)
{
	(void)saved_flags;
	assert(cpu == 0);
	g_poll_calls++;
	g_pending = false;
	g_cpu_irqlock = SP_UNLOCKED;
	return true;
}

bool up_cpu_pausereq(int cpu)
{
	(void)cpu;
	return false;
}

void up_cpu_paused_save(void) {}
bool up_cpu_paused(int cpu) { (void)cpu; return true; }
void up_cpu_paused_restore(void) {}

int main(void)
{
	irqstate_t flags;

	g_cpu_irqlock = SP_UNLOCKED;
	g_cpu_irqset = 0;
	g_tcb.irqcount = 0;
	g_irqsave_index = 0;
	g_restored_count = 0;
	g_pending = false;
	flags = enter_critical_section();
	assert(flags == 11);
	assert(g_tcb.irqcount == 1);
	leave_critical_section(flags);
	assert(g_tcb.irqcount == 0);
	assert(g_restored_count == 1 && g_restored[0] == 11);

	g_cpu_irqlock = SP_LOCKED;
	g_cpu_irqset = 0;
	g_tcb.irqcount = 0;
	g_pending = true;
	flags = enter_critical_section();
	assert(flags == 33);
	assert(g_poll_calls == 1);
	assert(g_tcb.irqcount == 1);
	leave_critical_section(flags);
	assert(g_tcb.irqcount == 0);
	assert(g_restored_count == 3);
	assert(g_restored[1] == 22 && g_restored[2] == 33);

	g_cpu_irqlock = SP_UNLOCKED;
	g_cpu_irqset = 0;
	g_tcb.irqcount = 0;
	flags = enter_critical_section();
	assert(flags == 44);
	leave_critical_section(flags);
	assert(g_tcb.irqcount == 0);
	assert(g_restored_count == 4 && g_restored[3] == 44);
	puts("PASS");
	return 0;
}
