#ifndef __KERNEL_DEBUG_MEM_LEAK_CHECKER_PAUSE_H
#define __KERNEL_DEBUG_MEM_LEAK_CHECKER_PAUSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mem_leak_checker_budget.h"

#define MLC_PAUSE_MAX_REMOTE_POLLS 65536u
#define MLC_PAUSE_REQUEST_TARGET_US 10000u
#define MLC_PAUSE_ACCEPT_TARGET_US  20000u
#define MLC_PAUSE_CANCEL_TARGET_US  40000u
#define MLC_PAUSE_WORK_STOP_US      78000u
#define MLC_PAUSE_RESUME_TARGET_US  80000u
#define MLC_PAUSE_TERMINAL_LIMIT_US 95000u

static inline bool mlc_pause_deadline_after(uint64_t epoch_usec,
		uint64_t threshold_usec, uint64_t *deadline_usec)
{
	if (deadline_usec == NULL || epoch_usec == 0 ||
		threshold_usec > UINT64_MAX - epoch_usec) {
		return false;
	}
	*deadline_usec = epoch_usec + threshold_usec;
	return true;
}

enum mlc_pause_state_e {
	MLC_PAUSE_IDLE = 0,
	MLC_PAUSE_PAUSE_REQ,
	MLC_PAUSE_CLAIMED_IRQ,
	MLC_PAUSE_CLAIMED_POLL,
	MLC_PAUSE_PAUSED_IRQ,
	MLC_PAUSE_PAUSED_POLL,
	MLC_PAUSE_CANCEL_REQ_UNCLAIMED,
	MLC_PAUSE_CANCEL_REQ_IRQ,
	MLC_PAUSE_CANCEL_REQ_POLL,
	MLC_PAUSE_RESUME_REQ_IRQ,
	MLC_PAUSE_RESUME_REQ_POLL,
	MLC_PAUSE_CANCELLED,
	MLC_PAUSE_RESUMED,
	MLC_PAUSE_FATAL
};

enum mlc_pause_service_e {
	MLC_PAUSE_SERVICE_NONE = 0,
	MLC_PAUSE_SERVICE_IRQ,
	MLC_PAUSE_SERVICE_POLL
};

enum mlc_fatal_reason_e {
	MLC_PAUSE_FATAL_NONE = 0,
	MLC_PAUSE_FATAL_RESUME_AMBIGUOUS,
	MLC_PAUSE_FATAL_CANCEL_AMBIGUOUS,
	MLC_PAUSE_FATAL_REMOTE_COUNTER_EXHAUSTED,
	MLC_PAUSE_FATAL_CLOCK_INVALID,
	MLC_PAUSE_FATAL_MAILBOX_PROTOCOL
};

enum mlc_pause_step_e {
	MLC_PAUSE_STEP_WAIT = 0,
	MLC_PAUSE_STEP_TERMINAL,
	MLC_PAUSE_STEP_FATAL
};

struct mlc_pause_saved_context_s {
	uint32_t magic;
	uint16_t version;
	uint16_t words;
	uint32_t flags;
	uint32_t callee_saved[8];
	uint32_t stack_pointer;
	uint32_t caller_boundary;
	uint32_t status;
	uint32_t exception;
	uint32_t cpu;
	uint32_t tcb;
	uint32_t callee_saved_mask;
};

struct mlc_pause_mailbox_s {
	volatile uint32_t token;
	volatile uint32_t state;
	volatile uint32_t service_path;
	volatile uint32_t request_pending;
	volatile uint32_t sgi_outstanding;
	volatile uint32_t remote_wait_remaining;
	volatile uint32_t fatal_reason;
	volatile uint64_t epoch_usec;
	volatile uint64_t last_remote_usec;
	volatile uint32_t clock_stall_remaining;
	struct mlc_pause_saved_context_s saved_context;
};

void mlc_pause_mailbox_init(struct mlc_pause_mailbox_s *mailbox);
int mlc_pause_next_token(uint32_t *token);
int mlc_pause_publish(struct mlc_pause_mailbox_s *mailbox, uint32_t token,
		uint32_t remote_max, uint64_t epoch_usec,
		const struct mlc_pause_saved_context_s *saved);
int mlc_pause_claim(struct mlc_pause_mailbox_s *mailbox, uint32_t token,
		enum mlc_pause_service_e path, bool initial_irqs_enabled,
		const struct mlc_pause_saved_context_s *saved);
int mlc_pause_owner_cancel(struct mlc_pause_mailbox_s *mailbox,
		uint32_t token);
int mlc_pause_owner_resume(struct mlc_pause_mailbox_s *mailbox,
		uint32_t token);
enum mlc_pause_step_e mlc_pause_remote_step(
		struct mlc_pause_mailbox_s *mailbox, uint32_t token,
		uint64_t now, uint64_t terminal_deadline);
int mlc_pause_sgi_drain(struct mlc_pause_mailbox_s *mailbox,
		uint32_t token);
int mlc_pause_owner_recycle(struct mlc_pause_mailbox_s *mailbox,
		uint32_t token);
int mlc_pause_abort_unsent(struct mlc_pause_mailbox_s *mailbox,
		uint32_t token);
enum mlc_pause_state_e mlc_pause_state(
		const struct mlc_pause_mailbox_s *mailbox);
enum mlc_fatal_reason_e mlc_pause_fatal_reason(
		const struct mlc_pause_mailbox_s *mailbox);
void mlc_pause_force_fatal(struct mlc_pause_mailbox_s *mailbox,
		enum mlc_fatal_reason_e reason);

#ifndef MLC_PAUSE_HOST_TEST
bool mlc_pause_poll_pending(int cpu, uintptr_t saved_flags);
bool mlc_pause_service_poll(int cpu, uintptr_t saved_flags);
bool mlc_pause_service_irq(int cpu, int irq, const void *context,
		uint32_t token);
uint32_t mlc_pause_service_token(int cpu);
int mlc_pause_owner_request_cpu(int cpu, uint32_t token, uint64_t epoch_usec);
enum mlc_pause_state_e mlc_pause_owner_state_cpu(int cpu, uint32_t token);
int mlc_pause_owner_cancel_cpu(int cpu, uint32_t token);
int mlc_pause_owner_resume_cpu(int cpu, uint32_t token);
int mlc_pause_owner_recycle_cpu(int cpu, uint32_t token);
bool mlc_pause_owner_drained_cpu(int cpu, uint32_t token);
void mlc_pause_fatal_dispatch(enum mlc_fatal_reason_e reason);
#endif

#endif
