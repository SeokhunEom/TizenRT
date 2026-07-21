#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "mem_leak_checker_pause.h"

static int mlc_pause_budget_take(enum mlc_budget_counter_e counter)
{
	struct mlc_budget_counters_s *budget = mlc_budget_current();
	int ret;

	if (budget == NULL) {
		return -EPERM;
	}
	ret = mlc_budget_chunk_begin(budget, counter, 1,
		mlc_budget_clock_now());
	return ret < 0 ? ret : mlc_budget_chunk_end(budget,
		mlc_budget_clock_now());
}

static uint32_t load_acquire(const volatile uint32_t *value)
{
	return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void store_release(volatile uint32_t *value, uint32_t next)
{
	__atomic_store_n(value, next, __ATOMIC_RELEASE);
}

static bool token_matches(const struct mlc_pause_mailbox_s *mailbox,
		uint32_t token)
{
	return token != 0 && load_acquire(&mailbox->token) == token;
}

static enum mlc_pause_step_e set_fatal(struct mlc_pause_mailbox_s *mailbox,
		enum mlc_fatal_reason_e reason)
{
	store_release(&mailbox->fatal_reason, (uint32_t)reason);
	store_release(&mailbox->state, MLC_PAUSE_FATAL);
	return MLC_PAUSE_STEP_FATAL;
}

enum mlc_pause_step_e mlc_pause_remote_step(
		struct mlc_pause_mailbox_s *mailbox, uint32_t token,
		uint64_t now, uint64_t terminal_deadline)
{
	uint32_t state;
	uint32_t remaining;
	uint32_t stalled;
	uint64_t last;

	if (mailbox == NULL || !token_matches(mailbox, token)) {
		return mailbox == NULL ? MLC_PAUSE_STEP_FATAL :
			set_fatal(mailbox, MLC_PAUSE_FATAL_MAILBOX_PROTOCOL);
	}
	state = load_acquire(&mailbox->state);
	if (state == MLC_PAUSE_RESUME_REQ_IRQ ||
		state == MLC_PAUSE_RESUME_REQ_POLL) {
		store_release(&mailbox->state, MLC_PAUSE_RESUMED);
		return MLC_PAUSE_STEP_TERMINAL;
	}
	if (state == MLC_PAUSE_CANCEL_REQ_IRQ ||
		state == MLC_PAUSE_CANCEL_REQ_POLL) {
		store_release(&mailbox->state, MLC_PAUSE_CANCELLED);
		return MLC_PAUSE_STEP_TERMINAL;
	}
	if (state != MLC_PAUSE_PAUSED_IRQ && state != MLC_PAUSE_PAUSED_POLL) {
		return set_fatal(mailbox, MLC_PAUSE_FATAL_MAILBOX_PROTOCOL);
	}
	if (mlc_pause_budget_take(MLC_BUDGET_REMOTE_PAUSED_SERVICE) < 0) {
		return set_fatal(mailbox, MLC_PAUSE_FATAL_REMOTE_COUNTER_EXHAUSTED);
	}
	remaining = load_acquire(&mailbox->remote_wait_remaining);
	for (;;) {
		uint32_t next;

		if (remaining == 0) {
			return set_fatal(mailbox, MLC_PAUSE_FATAL_REMOTE_COUNTER_EXHAUSTED);
		}
		next = remaining - 1;
		if (__atomic_compare_exchange_n(&mailbox->remote_wait_remaining,
			&remaining, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			remaining = next;
			break;
		}
	}
	last = __atomic_load_n(&mailbox->last_remote_usec, __ATOMIC_ACQUIRE);
	if (now == 0 || now < last) {
		return set_fatal(mailbox, MLC_PAUSE_FATAL_CLOCK_INVALID);
	}
	if (now == last) {
		stalled = __atomic_fetch_sub(&mailbox->clock_stall_remaining, 1,
			__ATOMIC_ACQ_REL);
		if (stalled <= 1) {
			return set_fatal(mailbox, MLC_PAUSE_FATAL_CLOCK_INVALID);
		}
	} else {
		__atomic_store_n(&mailbox->last_remote_usec, now, __ATOMIC_RELEASE);
		store_release(&mailbox->clock_stall_remaining, remaining);
	}
	if (terminal_deadline == 0 || terminal_deadline <
		__atomic_load_n(&mailbox->epoch_usec, __ATOMIC_ACQUIRE) ||
		now >= terminal_deadline) {
		return set_fatal(mailbox, MLC_PAUSE_FATAL_CLOCK_INVALID);
	}
	return MLC_PAUSE_STEP_WAIT;
}

int mlc_pause_sgi_drain(struct mlc_pause_mailbox_s *mailbox,
		uint32_t token)
{
	uint32_t expected = 1;
	uint32_t state;

	if (mailbox == NULL || !token_matches(mailbox, token)) {
		return -ESTALE;
	}
	state = load_acquire(&mailbox->state);
	if ((state != MLC_PAUSE_CANCELLED && state != MLC_PAUSE_RESUMED) ||
		load_acquire(&mailbox->service_path) != MLC_PAUSE_SERVICE_POLL) {
		set_fatal(mailbox, MLC_PAUSE_FATAL_MAILBOX_PROTOCOL);
		return -EPROTO;
	}
	if (mlc_pause_budget_take(MLC_BUDGET_SGI_DRAIN) < 0) {
		return -E2BIG;
	}
	if (!__atomic_compare_exchange_n(&mailbox->sgi_outstanding, &expected, 0,
		false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		set_fatal(mailbox, MLC_PAUSE_FATAL_MAILBOX_PROTOCOL);
		return -EPROTO;
	}
	return 0;
}

int mlc_pause_owner_recycle(struct mlc_pause_mailbox_s *mailbox,
		uint32_t token)
{
	uint32_t state;

	if (mailbox == NULL || !token_matches(mailbox, token)) {
		return -ESTALE;
	}
	state = load_acquire(&mailbox->state);
	if ((state != MLC_PAUSE_CANCELLED && state != MLC_PAUSE_RESUMED) ||
		load_acquire(&mailbox->request_pending) != 0 ||
		load_acquire(&mailbox->sgi_outstanding) != 0) {
		return -EBUSY;
	}
	memset(&mailbox->saved_context, 0, sizeof(mailbox->saved_context));
	store_release(&mailbox->service_path, MLC_PAUSE_SERVICE_NONE);
	store_release(&mailbox->remote_wait_remaining, 0);
	store_release(&mailbox->clock_stall_remaining, 0);
	__atomic_store_n(&mailbox->epoch_usec, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&mailbox->last_remote_usec, 0, __ATOMIC_RELEASE);
	store_release(&mailbox->fatal_reason, MLC_PAUSE_FATAL_NONE);
	store_release(&mailbox->token, 0);
	store_release(&mailbox->state, MLC_PAUSE_IDLE);
	return 0;
}

enum mlc_pause_state_e mlc_pause_state(
		const struct mlc_pause_mailbox_s *mailbox)
{
	return mailbox == NULL ? MLC_PAUSE_FATAL :
		(enum mlc_pause_state_e)load_acquire(&mailbox->state);
}

enum mlc_fatal_reason_e mlc_pause_fatal_reason(
		const struct mlc_pause_mailbox_s *mailbox)
{
	return mailbox == NULL ? MLC_PAUSE_FATAL_MAILBOX_PROTOCOL :
		(enum mlc_fatal_reason_e)load_acquire(&mailbox->fatal_reason);
}

void mlc_pause_force_fatal(struct mlc_pause_mailbox_s *mailbox,
		enum mlc_fatal_reason_e reason)
{
	if (mailbox != NULL && reason != MLC_PAUSE_FATAL_NONE) {
		set_fatal(mailbox, reason);
	}
}

int mlc_pause_abort_unsent(struct mlc_pause_mailbox_s *mailbox,
		uint32_t token)
{
	uint32_t expected = MLC_PAUSE_PAUSE_REQ;

	if (mailbox == NULL || !token_matches(mailbox, token)) {
		return -ESTALE;
	}
	if (!__atomic_compare_exchange_n(&mailbox->state, &expected,
		MLC_PAUSE_CANCELLED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		return -EBUSY;
	}
	store_release(&mailbox->request_pending, 0);
	store_release(&mailbox->sgi_outstanding, 0);
	return 0;
}
