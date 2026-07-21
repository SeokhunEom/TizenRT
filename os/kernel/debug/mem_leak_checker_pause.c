#ifndef MLC_PAUSE_HOST_TEST
#include <tinyara/config.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#ifndef MLC_PAUSE_HOST_TEST
#include <arch/irq.h>
#include <tinyara/arch.h>
#include <tinyara/spinlock.h>
#endif

#include "mem_leak_checker_pause.h"

#ifdef MLC_PAUSE_HOST_TEST
extern void mlc_pause_test_dmb(void);
extern void mlc_pause_test_store_order(int order);
#define SP_DMB() mlc_pause_test_dmb()
#define MLC_PAUSE_RELEASE_ORDER __ATOMIC_RELEASE
#endif

static uint32_t load_acquire(const volatile uint32_t *value)
{
	return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void store_release(volatile uint32_t *value, uint32_t next)
{
#ifdef MLC_PAUSE_HOST_TEST
	mlc_pause_test_store_order(MLC_PAUSE_RELEASE_ORDER);
	__atomic_store_n(value, next, MLC_PAUSE_RELEASE_ORDER);
#else
	__atomic_store_n(value, next, __ATOMIC_RELEASE);
#endif
}

static bool compare_acq_rel(volatile uint32_t *value, uint32_t *expected,
		uint32_t next)
{
	return __atomic_compare_exchange_n(value, expected, next, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
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

void mlc_pause_mailbox_init(struct mlc_pause_mailbox_s *mailbox)
{
	if (mailbox != NULL) {
		memset(mailbox, 0, sizeof(*mailbox));
	}
}

int mlc_pause_next_token(uint32_t *token)
{
	if (token == NULL) {
		return -EINVAL;
	}
	if (*token == UINT32_MAX) {
		return -EOVERFLOW;
	}
	(*token)++;
	return *token == 0 ? -EOVERFLOW : 0;
}

int mlc_pause_publish(struct mlc_pause_mailbox_s *mailbox, uint32_t token,
		uint32_t remote_max, uint64_t epoch_usec,
		const struct mlc_pause_saved_context_s *saved)
{
	uint32_t expected = MLC_PAUSE_IDLE;
	uint32_t empty_token = 0;

	if (mailbox == NULL || saved == NULL || token == 0 || epoch_usec == 0) {
		return -EINVAL;
	}
	if (remote_max == 0 || remote_max > MLC_PAUSE_MAX_REMOTE_POLLS ||
		remote_max > CONFIG_MEM_LEAK_CHECKER_REMOTE_PAUSED_SERVICE_MAX) {
		return -ERANGE;
	}
	if (load_acquire(&mailbox->request_pending) != 0 ||
		load_acquire(&mailbox->sgi_outstanding) != 0) {
		return -EBUSY;
	}
	if (!__atomic_compare_exchange_n(&mailbox->token, &empty_token, token,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		return -EBUSY;
	}

	memcpy(&mailbox->saved_context, saved, sizeof(*saved));
	store_release(&mailbox->remote_wait_remaining, remote_max);
	__atomic_store_n(&mailbox->epoch_usec, epoch_usec, __ATOMIC_RELEASE);
	__atomic_store_n(&mailbox->last_remote_usec, epoch_usec, __ATOMIC_RELEASE);
	store_release(&mailbox->clock_stall_remaining, remote_max);
	store_release(&mailbox->service_path, MLC_PAUSE_SERVICE_NONE);
	store_release(&mailbox->fatal_reason, MLC_PAUSE_FATAL_NONE);
	if (!compare_acq_rel(&mailbox->state, &expected, MLC_PAUSE_PAUSE_REQ)) {
		store_release(&mailbox->token, 0);
		return -EBUSY;
	}
	store_release(&mailbox->request_pending, 1);
	store_release(&mailbox->sgi_outstanding, 1);
	SP_DMB();
	return 0;
}

int mlc_pause_claim(struct mlc_pause_mailbox_s *mailbox, uint32_t token,
		enum mlc_pause_service_e path, bool initial_irqs_enabled,
		const struct mlc_pause_saved_context_s *saved)
{
	uint32_t expected;
	uint32_t claimed;
	uint32_t paused;

	if (mailbox == NULL || saved == NULL ||
		(path != MLC_PAUSE_SERVICE_IRQ &&
		path != MLC_PAUSE_SERVICE_POLL)) {
		return -EINVAL;
	}
	if (path == MLC_PAUSE_SERVICE_POLL && !initial_irqs_enabled) {
		return -EPERM;
	}
	if (!token_matches(mailbox, token)) {
		return -ESTALE;
	}

	expected = MLC_PAUSE_PAUSE_REQ;
	claimed = path == MLC_PAUSE_SERVICE_IRQ ? MLC_PAUSE_CLAIMED_IRQ :
		MLC_PAUSE_CLAIMED_POLL;
	paused = path == MLC_PAUSE_SERVICE_IRQ ? MLC_PAUSE_PAUSED_IRQ :
		MLC_PAUSE_PAUSED_POLL;
	if (compare_acq_rel(&mailbox->state, &expected, claimed)) {
		store_release(&mailbox->service_path, (uint32_t)path);
		store_release(&mailbox->request_pending, 0);
		memcpy(&mailbox->saved_context, saved, sizeof(*saved));
		if (path == MLC_PAUSE_SERVICE_IRQ) {
			store_release(&mailbox->sgi_outstanding, 0);
		}
		expected = claimed;
		if (compare_acq_rel(&mailbox->state, &expected, paused)) {
			return 0;
		}
		if ((path == MLC_PAUSE_SERVICE_IRQ &&
			expected == MLC_PAUSE_CANCEL_REQ_IRQ) ||
			(path == MLC_PAUSE_SERVICE_POLL &&
			expected == MLC_PAUSE_CANCEL_REQ_POLL)) {
			uint32_t cancel = expected;
			return compare_acq_rel(&mailbox->state, &cancel,
				MLC_PAUSE_CANCELLED) ? 0 : -EAGAIN;
		}
		set_fatal(mailbox, MLC_PAUSE_FATAL_MAILBOX_PROTOCOL);
		return -EPROTO;
	}

	expected = MLC_PAUSE_CANCEL_REQ_UNCLAIMED;
	if (compare_acq_rel(&mailbox->state, &expected, MLC_PAUSE_CANCELLED)) {
		store_release(&mailbox->service_path, (uint32_t)path);
		store_release(&mailbox->request_pending, 0);
		if (path == MLC_PAUSE_SERVICE_IRQ) {
			store_release(&mailbox->sgi_outstanding, 0);
		}
		return 0;
	}
	return -EALREADY;
}

int mlc_pause_owner_cancel(struct mlc_pause_mailbox_s *mailbox,
		uint32_t token)
{
	uint32_t expected;

	if (mailbox == NULL || !token_matches(mailbox, token)) {
		return -ESTALE;
	}
	expected = MLC_PAUSE_PAUSE_REQ;
	if (compare_acq_rel(&mailbox->state, &expected,
		MLC_PAUSE_CANCEL_REQ_UNCLAIMED)) {
		return 0;
	}
	expected = MLC_PAUSE_CLAIMED_IRQ;
	if (compare_acq_rel(&mailbox->state, &expected, MLC_PAUSE_CANCEL_REQ_IRQ)) {
		return 0;
	}
	expected = MLC_PAUSE_CLAIMED_POLL;
	if (compare_acq_rel(&mailbox->state, &expected, MLC_PAUSE_CANCEL_REQ_POLL)) {
		return 0;
	}
	expected = MLC_PAUSE_PAUSED_IRQ;
	if (compare_acq_rel(&mailbox->state, &expected, MLC_PAUSE_RESUME_REQ_IRQ)) {
		return 0;
	}
	expected = MLC_PAUSE_PAUSED_POLL;
	return compare_acq_rel(&mailbox->state, &expected,
		MLC_PAUSE_RESUME_REQ_POLL) ? 0 : -EALREADY;
}

int mlc_pause_owner_resume(struct mlc_pause_mailbox_s *mailbox,
		uint32_t token)
{
	uint32_t expected;

	if (mailbox == NULL || !token_matches(mailbox, token)) {
		return -ESTALE;
	}
	expected = MLC_PAUSE_PAUSED_IRQ;
	if (compare_acq_rel(&mailbox->state, &expected, MLC_PAUSE_RESUME_REQ_IRQ)) {
		return 0;
	}
	expected = MLC_PAUSE_PAUSED_POLL;
	return compare_acq_rel(&mailbox->state, &expected,
		MLC_PAUSE_RESUME_REQ_POLL) ? 0 : -EALREADY;
}
