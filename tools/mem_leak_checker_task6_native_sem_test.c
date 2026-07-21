#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <arch/irq.h>
#include <tinyara/irq.h>
#include <tinyara/mm/mm.h>

#include "mem_leak_checker_task6_native_support.h"

static const char *variant_name(void)
{
#ifdef CONFIG_SMP
	return "smp_irqcount";
#elif defined(CONFIG_IRQCOUNT)
	return "up_irqcount";
#else
	return "up_no_irqcount";
#endif
}

static void assert_acquired(struct mm_heap_s *heap, struct tcb_s *tcb)
{
	assert(heap->mm_holder == getpid());
	assert(heap->mm_counts_held == 1);
	assert(heap->mm_semaphore.semcount == 0);
	assert(heap->mm_semaphore.holder.htcb == tcb);
	assert(heap->mm_semaphore.holder.counts == 1);
	assert(tcb->holdsem == &heap->mm_semaphore.holder);
	assert(g_native_history_count == 1);
	assert(g_native_history[0] == SEM_ACQUIRE);
	assert(g_native_history_depth[0] == 2);
}

static void assert_released(struct mm_heap_s *heap, struct tcb_s *tcb)
{
	assert(heap->mm_holder == -1);
	assert(heap->mm_counts_held == 0);
	assert(heap->mm_semaphore.semcount == 1);
	assert(heap->mm_semaphore.holder.htcb == NULL);
	assert(heap->mm_semaphore.holder.counts == 0);
	assert(tcb->holdsem == NULL);
	assert(g_native_history_count == 2);
	assert(g_native_history[1] == SEM_RELEASE);
	assert(g_native_history_depth[1] == 2);
	assert(g_native_sched_locks == 1);
	assert(g_native_sched_unlocks == 1);
}

int main(void)
{
	struct mm_heap_s heap;
	struct tcb_s *tcb;
	irqstate_t outer_flags = 0x55;

	mlc_native_reset();
	mm_seminitialize(&heap);
	tcb = mlc_native_tcb();

	assert(irq_try_enter_critical_fresh(&outer_flags) == 0);
	assert(outer_flags == 0);
	assert(mlc_native_irq_state() == 1);
	assert(g_native_irq_depth == 1);
#ifdef CONFIG_IRQCOUNT
	assert(tcb->irqcount == 1);
#endif

	mlc_native_set_phase(MLC_NATIVE_PHASE_ACQUIRE);
	assert(mm_trysemaphore_fresh(&heap) == 0);
	mlc_native_set_phase(MLC_NATIVE_PHASE_NONE);
	assert_acquired(&heap, tcb);
	assert(g_native_irq_depth == 1);
#ifdef CONFIG_IRQCOUNT
	assert(tcb->irqcount == 1);
#endif
	assert(g_native_phase_spin[MLC_NATIVE_PHASE_ACQUIRE] == 0);
	assert(g_native_pause_probes == 0);

	assert(mm_takesemaphore(&heap));
	assert(heap.mm_counts_held == 2);
	assert(heap.mm_semaphore.holder.counts == 1);
	assert(g_native_sem_wait_calls == 0);
	assert(g_native_history_count == 1);

	mm_givesemaphore(&heap);
	assert(heap.mm_counts_held == 1);
	assert(heap.mm_semaphore.holder.counts == 1);
	assert(g_native_history_count == 1);

	mlc_native_set_phase(MLC_NATIVE_PHASE_RELEASE);
	mm_givesemaphore(&heap);
	mlc_native_set_phase(MLC_NATIVE_PHASE_NONE);
	assert_released(&heap, tcb);
	assert(g_native_irq_depth == 1);
#ifdef CONFIG_IRQCOUNT
	assert(tcb->irqcount == 1);
#endif
	assert(g_native_phase_spin[MLC_NATIVE_PHASE_RELEASE] == 0);
	assert(g_native_pause_probes == 0);
	assert(g_native_irq_max == 2);

	leave_critical_section(outer_flags);
	assert(g_native_irq_depth == 0);
	assert(mlc_native_irq_state() == 0);
#ifdef CONFIG_IRQCOUNT
	assert(tcb->irqcount == 0);
#endif

	printf("MLC_TASK6_NATIVE_SEM status=PASS variant=%s nested=1,2,1 "
			"holder_pi_history=true irqwaitlock_acquire=0 irqwaitlock_release=0\n",
			variant_name());
	return 0;
}
