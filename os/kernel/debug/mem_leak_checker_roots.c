#include <tinyara/config.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <tinyara/arch.h>
#include <tinyara/compiler.h>
#include <tinyara/irq.h>
#include <tinyara/sched.h>

#include "sched/sched.h"
#include "mem_leak_checker_roots.h"

_Static_assert(sizeof(struct up_mem_leak_capture_s) ==
	UP_MEM_LEAK_CAPTURE_SIZE, "capture size drift");
_Static_assert(offsetof(struct up_mem_leak_capture_s, callee_saved) ==
	UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET, "callee offset drift");
_Static_assert(offsetof(struct up_mem_leak_capture_s, stack_pointer) ==
	UP_MEM_LEAK_CAPTURE_SP_OFFSET, "stack offset drift");
_Static_assert(offsetof(struct up_mem_leak_capture_s, caller_boundary) ==
	UP_MEM_LEAK_CAPTURE_BOUNDARY_OFFSET, "boundary offset drift");
_Static_assert(offsetof(struct up_mem_leak_capture_s, status) ==
	UP_MEM_LEAK_CAPTURE_STATUS_OFFSET, "status offset drift");
_Static_assert(offsetof(struct up_mem_leak_capture_s, exception) ==
	UP_MEM_LEAK_CAPTURE_EXCEPTION_OFFSET, "exception offset drift");
_Static_assert(offsetof(struct up_mem_leak_capture_s, cpu) ==
	UP_MEM_LEAK_CAPTURE_CPU_OFFSET, "cpu offset drift");
_Static_assert(offsetof(struct up_mem_leak_capture_s, tcb) ==
	UP_MEM_LEAK_CAPTURE_TCB_OFFSET, "TCB offset drift");
_Static_assert(offsetof(struct up_mem_leak_capture_s, callee_saved_mask) ==
	UP_MEM_LEAK_CAPTURE_MASK_OFFSET, "mask offset drift");

static bool mlc_stack_bounds(const struct tcb_s *tcb, uintptr_t *low,
		uintptr_t *high)
{
	uintptr_t adjusted = (uintptr_t)tcb->adj_stack_ptr;

#ifdef CONFIG_ARCH_ARMV7A_FAMILY
	if (adjusted < tcb->adj_stack_size) {
		return false;
	}
	*low = adjusted - tcb->adj_stack_size;
	*high = adjusted;
#else
	if (adjusted > UINTPTR_MAX - sizeof(uint32_t) ||
		adjusted + sizeof(uint32_t) < tcb->adj_stack_size) {
		return false;
	}
	*high = adjusted + sizeof(uint32_t);
	*low = *high - tcb->adj_stack_size;
#endif
	return *low <= *high;
}

void up_mem_leak_capture_identity(struct up_mem_leak_capture_s *capture)
{
	irqstate_t flags;
	struct tcb_s *tcb;
	int cpu;

	if (capture == NULL || capture->magic != UP_MEM_LEAK_CAPTURE_MAGIC) {
		return;
	}

	flags = irqsave();
	cpu = this_cpu();
	tcb = current_task(cpu);
	capture->cpu = (uint32_t)cpu;
	capture->tcb = (uint32_t)(uintptr_t)tcb;
	irqrestore(flags);
}

#if !defined(CONFIG_ARCH_CHIP_LM) && !defined(CONFIG_ARCH_CHIP_AMEBASMART)
void weak_function up_mem_leak_capture_current(
		struct up_mem_leak_capture_s *capture)
{
	if (capture != NULL) {
		memset(capture, 0, sizeof(*capture));
	}
}
#endif

bool mlc_validate_current_capture(
		const struct up_mem_leak_capture_s *capture)
{
	irqstate_t flags;
	struct tcb_s *tcb;
	uintptr_t stack_high;
	uintptr_t stack_low;
	bool valid = false;
	int cpu;

	if (capture == NULL || capture->magic != UP_MEM_LEAK_CAPTURE_MAGIC ||
		capture->version != UP_MEM_LEAK_CAPTURE_VERSION ||
		capture->words != UP_MEM_LEAK_CAPTURE_WORDS ||
		capture->exception != 0 ||
		capture->callee_saved_mask != UP_MEM_LEAK_CAPTURE_CALLEE_MASK ||
		(capture->stack_pointer & 7u) != 0) {
		return false;
	}

#ifdef CONFIG_ARCH_ARMV7A_FAMILY
	if (capture->flags != (UP_MEM_LEAK_CAPTURE_FLAG_TASK |
		UP_MEM_LEAK_CAPTURE_FLAG_ARMV7_A) || (capture->status & (1u << 7))) {
		return false;
	}
#elif defined(CONFIG_ARCH_CORTEXM3) || defined(CONFIG_ARCH_CORTEXM4) || \
	defined(CONFIG_ARCH_CORTEXM7)
	if (capture->flags != (UP_MEM_LEAK_CAPTURE_FLAG_TASK |
		UP_MEM_LEAK_CAPTURE_FLAG_ARMV7_M) || capture->status != 0) {
		return false;
	}
#else
	return false;
#endif

	if (irq_try_enter_critical_fresh(&flags) < 0) {
		return false;
	}

	cpu = this_cpu();
	tcb = current_task(cpu);
	if (tcb == NULL || tcb->task_state != TSTATE_TASK_RUNNING ||
		capture->tcb != (uint32_t)(uintptr_t)tcb ||
		capture->cpu != (uint32_t)cpu) {
		goto out;
	}
#ifdef CONFIG_SMP
	if (tcb->cpu != capture->cpu) {
		goto out;
	}
#endif

	if (!mlc_stack_bounds(tcb, &stack_low, &stack_high)) {
		goto out;
	}
	if (capture->stack_pointer < stack_low ||
		capture->stack_pointer > stack_high ||
		capture->caller_boundary < capture->stack_pointer ||
		capture->caller_boundary > stack_high ||
		(capture->caller_boundary & 7u) != 0) {
		goto out;
	}

	valid = true;
out:
	leave_critical_section(flags);
	return valid;
}

static bool mlc_validate_saved_status(const uint32_t *registers,
		enum mlc_saved_context_mode_e mode)
{
#ifdef CONFIG_ARCH_ARMV7A_FAMILY
	uint32_t cpsr = registers[REG_CPSR];
	uint32_t processor_mode = cpsr & 0x1fu;

	if (processor_mode != 0x10u && processor_mode != 0x13u &&
		processor_mode != 0x1fu) {
		return false;
	}
	(void)mode;
	if ((cpsr & 0x00f00000u) != 0) {
		return false;
	}
	return true;
#elif defined(CONFIG_ARCH_CORTEXM3) || defined(CONFIG_ARCH_CORTEXM4) || \
	defined(CONFIG_ARCH_CORTEXM7)
	uint32_t xpsr = registers[REG_XPSR];

	(void)mode;
	return (xpsr & (1u << 24)) != 0 && (xpsr & 0x1ffu) == 0;
#else
	(void)registers;
	(void)mode;
	return false;
#endif
}

bool mlc_validate_saved_task_roots(const struct tcb_s *tcb,
		enum mlc_saved_context_mode_e mode, uint32_t expected_cpu,
		struct mlc_saved_task_roots_s *roots)
{
	const uint32_t *registers;
	uintptr_t stack_high;
	uintptr_t stack_low;
	uintptr_t saved_sp;

	if (tcb == NULL || roots == NULL || mode == MLC_CONTEXT_IRQ) {
		return false;
	}
	if (tcb->task_state == TSTATE_TASK_RUNNING) {
		if (mode != MLC_CONTEXT_REMOTE_PAUSED) {
			return false;
		}
#ifdef CONFIG_SMP
		if (tcb->cpu != expected_cpu) {
			return false;
		}
#else
		(void)expected_cpu;
		return false;
#endif
	} else if (mode != MLC_CONTEXT_BLOCKED) {
		return false;
	}

	registers = tcb->xcp.regs;
	if (registers == NULL) {
		return false;
	}
	if (!mlc_validate_saved_status(registers, mode)) {
		return false;
	}

	if (!mlc_stack_bounds(tcb, &stack_low, &stack_high)) {
		return false;
	}
	saved_sp = registers[REG_SP];
	if ((saved_sp & 7u) != 0 || saved_sp < stack_low ||
		saved_sp > stack_high) {
		return false;
	}

	roots->registers = registers;
	roots->register_count = XCPTCONTEXT_REGS;
	roots->stack_live_start = saved_sp;
	roots->stack_high = stack_high;
	return true;
}
