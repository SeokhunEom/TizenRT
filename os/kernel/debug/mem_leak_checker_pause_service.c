#ifndef MLC_PAUSE_HOST_TEST
#include <tinyara/config.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <arch/irq.h>
#include <tinyara/arch.h>

#include "sched/sched.h"
#include "mem_leak_checker_pause.h"

#if defined(CONFIG_SMP) && CONFIG_SMP_NCPUS == 2
static struct mlc_pause_mailbox_s g_mlc_pause_mailbox[CONFIG_SMP_NCPUS];

static uint32_t load_acquire(const volatile uint32_t *value)
{
	return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static bool cpu_valid(int cpu)
{
	return cpu >= 0 && cpu < CONFIG_SMP_NCPUS;
}

static void decode_poll_context(struct mlc_pause_saved_context_s *saved)
{
	struct up_mem_leak_capture_s capture;

	up_mem_leak_capture_current(&capture);
	saved->magic = capture.magic;
	saved->version = capture.version;
	saved->words = capture.words;
	saved->flags = capture.flags;
	memcpy(saved->callee_saved, capture.callee_saved,
		sizeof(saved->callee_saved));
	saved->stack_pointer = capture.stack_pointer;
	saved->caller_boundary = capture.caller_boundary;
	saved->status = capture.status;
	saved->exception = capture.exception;
	saved->cpu = capture.cpu;
	saved->tcb = capture.tcb;
	saved->callee_saved_mask = capture.callee_saved_mask;
}

static bool decode_irq_context(struct mlc_pause_saved_context_s *saved,
		int cpu, int irq, const void *context)
{
	const uint32_t *registers = context;
	struct tcb_s *tcb;
	size_t index;

	if (registers == NULL) {
		return false;
	}
	tcb = current_task(cpu);
	if (tcb == NULL) {
		return false;
	}
	memset(saved, 0, sizeof(*saved));
	saved->magic = UP_MEM_LEAK_CAPTURE_MAGIC;
	saved->version = UP_MEM_LEAK_CAPTURE_VERSION;
	saved->words = UP_MEM_LEAK_CAPTURE_WORDS;
	saved->flags = UP_MEM_LEAK_CAPTURE_FLAG_TASK |
		UP_MEM_LEAK_CAPTURE_FLAG_EXCEPTION | UP_MEM_LEAK_CAPTURE_FLAG_ARMV7_A;
	for (index = 0; index < 8; index++) {
		saved->callee_saved[index] = registers[REG_R4 + index];
	}
	saved->stack_pointer = registers[REG_SP];
	saved->caller_boundary = registers[REG_SP];
	saved->status = registers[REG_CPSR];
	saved->exception = (uint32_t)irq;
	saved->cpu = (uint32_t)cpu;
	saved->tcb = (uint32_t)(uintptr_t)tcb;
	saved->callee_saved_mask = UP_MEM_LEAK_CAPTURE_CALLEE_MASK;
	return true;
}

bool mlc_pause_poll_pending(int cpu, uintptr_t saved_flags)
{
	struct mlc_pause_mailbox_s *mailbox;
	uint32_t state;

	if (!cpu_valid(cpu) || !up_irq_saved_enabled((irqstate_t)saved_flags)) {
		return false;
	}
	mailbox = &g_mlc_pause_mailbox[cpu];
	state = load_acquire(&mailbox->state);
	return load_acquire(&mailbox->token) != 0 &&
		load_acquire(&mailbox->request_pending) != 0 &&
		(state == MLC_PAUSE_PAUSE_REQ ||
		 state == MLC_PAUSE_CANCEL_REQ_UNCLAIMED);
}

static bool service_cpu(int cpu, enum mlc_pause_service_e path,
		bool initial_irqs_enabled, int irq, const void *irq_context,
		uint32_t irq_token)
{
	struct mlc_pause_mailbox_s *mailbox;
	struct mlc_pause_saved_context_s saved;
	uint32_t token;
	uint64_t terminal_deadline;
	int result;

	if (!cpu_valid(cpu)) {
		return false;
	}
	if (path == MLC_PAUSE_SERVICE_POLL && !initial_irqs_enabled) {
		return false;
	}
	mailbox = &g_mlc_pause_mailbox[cpu];
	token = load_acquire(&mailbox->token);
	if (path == MLC_PAUSE_SERVICE_IRQ &&
		(irq_token == 0 || irq_token != token)) {
		return false;
	}
	if (path == MLC_PAUSE_SERVICE_IRQ && token != 0 &&
		(mlc_pause_state(mailbox) == MLC_PAUSE_CANCELLED ||
		 mlc_pause_state(mailbox) == MLC_PAUSE_RESUMED) &&
		load_acquire(&mailbox->service_path) == MLC_PAUSE_SERVICE_POLL) {
		if (mlc_pause_sgi_drain(mailbox, token) < 0) {
			mlc_pause_fatal_dispatch(mlc_pause_fatal_reason(mailbox));
		}
		return true;
	}
	if (path == MLC_PAUSE_SERVICE_IRQ) {
		if (!decode_irq_context(&saved, cpu, irq, irq_context)) {
			return false;
		}
	} else {
		decode_poll_context(&saved);
	}
	result = mlc_pause_claim(mailbox, token, path, initial_irqs_enabled,
		&saved);
	if (result < 0) {
		if (token != 0 &&
			(mlc_pause_state(mailbox) == MLC_PAUSE_CANCELLED ||
			 mlc_pause_state(mailbox) == MLC_PAUSE_RESUMED) &&
			load_acquire(&mailbox->service_path) == MLC_PAUSE_SERVICE_POLL &&
			path == MLC_PAUSE_SERVICE_IRQ) {
			if (mlc_pause_sgi_drain(mailbox, token) == 0) {
				return true;
			}
		}
		if (token != 0) {
			mlc_pause_force_fatal(mailbox, MLC_PAUSE_FATAL_MAILBOX_PROTOCOL);
			mlc_pause_fatal_dispatch(MLC_PAUSE_FATAL_MAILBOX_PROTOCOL);
		}
		return false;
	}
	if (!mlc_pause_deadline_after(
		__atomic_load_n(&mailbox->epoch_usec, __ATOMIC_ACQUIRE),
		MLC_PAUSE_TERMINAL_LIMIT_US, &terminal_deadline)) {
		mlc_pause_force_fatal(mailbox, MLC_PAUSE_FATAL_CLOCK_INVALID);
		mlc_pause_fatal_dispatch(MLC_PAUSE_FATAL_CLOCK_INVALID);
		return false;
	}
	while (mlc_pause_state(mailbox) != MLC_PAUSE_CANCELLED &&
		mlc_pause_state(mailbox) != MLC_PAUSE_RESUMED) {
		enum mlc_pause_step_e step = mlc_pause_remote_step(mailbox, token,
			up_mem_leak_monotonic_usec(),
			terminal_deadline);
		if (step == MLC_PAUSE_STEP_TERMINAL) {
			return true;
		}
		if (step == MLC_PAUSE_STEP_FATAL) {
			mlc_pause_fatal_dispatch(mlc_pause_fatal_reason(mailbox));
			return false;
		}
	}
	return true;
}

bool mlc_pause_service_poll(int cpu, uintptr_t saved_flags)
{
	return service_cpu(cpu, MLC_PAUSE_SERVICE_POLL,
		up_irq_saved_enabled((irqstate_t)saved_flags), 0, NULL, 0);
}

bool mlc_pause_service_irq(int cpu, int irq, const void *context,
		uint32_t token)
{
	return service_cpu(cpu, MLC_PAUSE_SERVICE_IRQ, false, irq, context, token);
}

uint32_t mlc_pause_service_token(int cpu)
{
	return cpu_valid(cpu) ? load_acquire(&g_mlc_pause_mailbox[cpu].token) : 0;
}

int mlc_pause_owner_request_cpu(int cpu, uint32_t token, uint64_t epoch_usec)
{
	struct mlc_pause_saved_context_s empty = {0};
	int result;

	if (!cpu_valid(cpu)) {
		return -EINVAL;
	}
	result = mlc_pause_publish(&g_mlc_pause_mailbox[cpu], token,
		CONFIG_MEM_LEAK_REMOTE_PAUSED_MAX_POLLS, epoch_usec, &empty);
	if (result < 0) {
		return result;
	}
	result = up_mem_leak_pause_request(cpu);
	if (result < 0) {
		if (mlc_pause_abort_unsent(&g_mlc_pause_mailbox[cpu], token) == 0) {
			mlc_pause_owner_recycle(&g_mlc_pause_mailbox[cpu], token);
			return result;
		}
		mlc_pause_force_fatal(&g_mlc_pause_mailbox[cpu],
			MLC_PAUSE_FATAL_CANCEL_AMBIGUOUS);
		mlc_pause_fatal_dispatch(MLC_PAUSE_FATAL_CANCEL_AMBIGUOUS);
	}
	return 0;
}

enum mlc_pause_state_e mlc_pause_owner_state_cpu(int cpu, uint32_t token)
{
	if (!cpu_valid(cpu) || load_acquire(&g_mlc_pause_mailbox[cpu].token) != token) {
		return MLC_PAUSE_FATAL;
	}
	return mlc_pause_state(&g_mlc_pause_mailbox[cpu]);
}

int mlc_pause_owner_cancel_cpu(int cpu, uint32_t token)
{
	return cpu_valid(cpu) ? mlc_pause_owner_cancel(&g_mlc_pause_mailbox[cpu],
		token) : -EINVAL;
}

int mlc_pause_owner_resume_cpu(int cpu, uint32_t token)
{
	return cpu_valid(cpu) ? mlc_pause_owner_resume(&g_mlc_pause_mailbox[cpu],
		token) : -EINVAL;
}

int mlc_pause_owner_recycle_cpu(int cpu, uint32_t token)
{
	return cpu_valid(cpu) ? mlc_pause_owner_recycle(&g_mlc_pause_mailbox[cpu],
		token) : -EINVAL;
}

bool mlc_pause_owner_drained_cpu(int cpu, uint32_t token)
{
	struct mlc_pause_mailbox_s *mailbox;

	if (!cpu_valid(cpu)) {
		return false;
	}
	mailbox = &g_mlc_pause_mailbox[cpu];
	return load_acquire(&mailbox->token) == token &&
		load_acquire(&mailbox->request_pending) == 0 &&
		load_acquire(&mailbox->sgi_outstanding) == 0;
}
#endif
#endif
