#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mem_leak_checker_pause.h"

static unsigned int g_dmb_count;
static unsigned int g_relaxed_store_count;
static struct mlc_budget_counters_s g_budget;

uint64_t up_mem_leak_monotonic_usec(void)
{
	return 1;
}

void mlc_pause_test_dmb(void)
{
	g_dmb_count++;
}

void mlc_pause_test_store_order(int order)
{
	if (order != __ATOMIC_RELEASE) {
		g_relaxed_store_count++;
	}
}

static void reset(struct mlc_pause_mailbox_s *mailbox)
{
	assert(mlc_budget_counters_init(&g_budget) == 0);
	mlc_budget_bind(&g_budget);
	mlc_pause_mailbox_init(mailbox);
	assert(mlc_pause_state(mailbox) == MLC_PAUSE_IDLE);
}

static void test_happy_paths(void)
{
	struct mlc_pause_mailbox_s mailbox;
	struct mlc_pause_saved_context_s saved = {0};
	uint32_t token = 0;

	reset(&mailbox);
	assert(mlc_pause_next_token(&token) == 0 && token == 1);
	assert(mlc_pause_publish(&mailbox, token, 3, 100, &saved) == 0);
	assert(mlc_pause_claim(&mailbox, token, MLC_PAUSE_SERVICE_IRQ, false,
		&saved) == 0);
	assert(mlc_pause_state(&mailbox) == MLC_PAUSE_PAUSED_IRQ);
	assert(mlc_pause_owner_resume(&mailbox, token) == 0);
	assert(mlc_pause_remote_step(&mailbox, token, 1, 95) == MLC_PAUSE_STEP_TERMINAL);
	assert(mlc_pause_state(&mailbox) == MLC_PAUSE_RESUMED);
	assert(mlc_pause_owner_recycle(&mailbox, token) == 0);

	reset(&mailbox);
	assert(mlc_pause_publish(&mailbox, token, 3, 100, &saved) == 0);
	assert(mlc_pause_claim(&mailbox, token, MLC_PAUSE_SERVICE_POLL, true,
		&saved) == 0);
	assert(mlc_pause_owner_resume(&mailbox, token) == 0);
	assert(mlc_pause_remote_step(&mailbox, token, 2, 95) == MLC_PAUSE_STEP_TERMINAL);
	assert(mlc_pause_sgi_drain(&mailbox, token) == 0);
	assert(mlc_pause_owner_recycle(&mailbox, token) == 0);
}

static void test_cancel_and_rejections(void)
{
	struct mlc_pause_mailbox_s mailbox;
	struct mlc_pause_saved_context_s saved = {0};
	uint32_t token = 7;

	reset(&mailbox);
	assert(mlc_pause_publish(&mailbox, token, 2, 100, &saved) == 0);
	assert(mlc_pause_owner_cancel(&mailbox, token) == 0);
	assert(mlc_pause_claim(&mailbox, token, MLC_PAUSE_SERVICE_IRQ, false,
		&saved) == 0);
	assert(mlc_pause_state(&mailbox) == MLC_PAUSE_CANCELLED);
	assert(mlc_pause_owner_recycle(&mailbox, token) == 0);

	reset(&mailbox);
	assert(mlc_pause_publish(&mailbox, token, 2, 100, &saved) == 0);
	{
		uint32_t expected = MLC_PAUSE_PAUSE_REQ;
		assert(__atomic_compare_exchange_n(&mailbox.state, &expected,
			MLC_PAUSE_CLAIMED_POLL, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE));
	}
	__atomic_store_n(&mailbox.service_path, MLC_PAUSE_SERVICE_POLL,
		__ATOMIC_RELEASE);
	__atomic_store_n(&mailbox.request_pending, 0, __ATOMIC_RELEASE);
	assert(mlc_pause_owner_cancel(&mailbox, token) == 0);
	assert(mlc_pause_remote_step(&mailbox, token, 1, 95) == MLC_PAUSE_STEP_TERMINAL);
	assert(mlc_pause_state(&mailbox) == MLC_PAUSE_CANCELLED);
	assert(mlc_pause_sgi_drain(&mailbox, token) == 0);
	assert(mlc_pause_owner_recycle(&mailbox, token) == 0);

	reset(&mailbox);
	assert(mlc_pause_publish(&mailbox, token, 2, 100, &saved) == 0);
	assert(mlc_pause_claim(&mailbox, token, MLC_PAUSE_SERVICE_POLL, false,
		&saved) == -EPERM);
	assert(mlc_pause_state(&mailbox) == MLC_PAUSE_PAUSE_REQ);
	assert(mlc_pause_claim(&mailbox, token + 1, MLC_PAUSE_SERVICE_IRQ, false,
		&saved) == -ESTALE);
	assert(mlc_pause_owner_cancel(&mailbox, token) == 0);
	assert(mlc_pause_claim(&mailbox, token, MLC_PAUSE_SERVICE_POLL, true,
		&saved) == 0);
	assert(mlc_pause_sgi_drain(&mailbox, token) == 0);
	assert(mlc_pause_owner_recycle(&mailbox, token) == 0);

	reset(&mailbox);
	assert(mlc_pause_publish(&mailbox, token, 2, 100, &saved) == 0);
	assert(mlc_pause_publish(&mailbox, token + 1, 2, 100, &saved) == -EBUSY);
	assert(mlc_pause_owner_cancel(&mailbox, token) == 0);
	assert(mlc_pause_claim(&mailbox, token, MLC_PAUSE_SERVICE_IRQ, false,
		&saved) == 0);
	assert(mlc_pause_state(&mailbox) == MLC_PAUSE_CANCELLED);
	assert(mlc_pause_owner_recycle(&mailbox, token) == 0);

	reset(&mailbox);
	__atomic_store_n(&mailbox.state, MLC_PAUSE_PAUSED_IRQ, __ATOMIC_RELEASE);
	assert(mlc_pause_publish(&mailbox, token, 2, 100, &saved) == -EBUSY);
	assert(__atomic_load_n(&mailbox.token, __ATOMIC_ACQUIRE) == 0);
}

static void test_counter_and_token_boundaries(void)
{
	struct mlc_pause_mailbox_s mailbox;
	struct mlc_pause_saved_context_s saved = {0};
	uint32_t token = UINT32_MAX - 1;

	assert(mlc_pause_next_token(&token) == 0 && token == UINT32_MAX);
	assert(mlc_pause_next_token(&token) == -EOVERFLOW && token == UINT32_MAX);
	reset(&mailbox);
	assert(mlc_pause_publish(&mailbox, 1, 0, 100, &saved) == -ERANGE);
	assert(mlc_pause_publish(&mailbox, 1, MLC_PAUSE_MAX_REMOTE_POLLS,
		100, &saved) == 0);
	assert(mlc_pause_abort_unsent(&mailbox, 1) == 0);
	assert(mlc_pause_owner_recycle(&mailbox, 1) == 0);
	assert(mlc_pause_publish(&mailbox, 1, MLC_PAUSE_MAX_REMOTE_POLLS + 1,
		100, &saved) == -ERANGE);
	assert(mlc_pause_publish(&mailbox, 1, 1, 100, &saved) == 0);
	assert(mlc_pause_claim(&mailbox, 1, MLC_PAUSE_SERVICE_IRQ, false,
		&saved) == 0);
	assert(mlc_pause_remote_step(&mailbox, 1, 101, 195) == MLC_PAUSE_STEP_WAIT);
	assert(mlc_pause_remote_step(&mailbox, 1, 102, 195) == MLC_PAUSE_STEP_FATAL);
	assert(mlc_pause_fatal_reason(&mailbox) == MLC_PAUSE_FATAL_REMOTE_COUNTER_EXHAUSTED);
}

static void test_remote_exact_max_then_exhaustion(void)
{
	struct mlc_pause_mailbox_s mailbox;
	struct mlc_pause_saved_context_s saved = {0};
	uint32_t index;

	reset(&mailbox);
	assert(mlc_pause_publish(&mailbox, 13, MLC_PAUSE_MAX_REMOTE_POLLS,
		100, &saved) == 0);
	assert(mlc_pause_claim(&mailbox, 13, MLC_PAUSE_SERVICE_IRQ, false,
		&saved) == 0);
	for (index = 0; index < MLC_PAUSE_MAX_REMOTE_POLLS; index++) {
		assert(mlc_pause_remote_step(&mailbox, 13, 101 + index, UINT64_MAX) ==
			MLC_PAUSE_STEP_WAIT);
	}
	assert(mlc_pause_remote_step(&mailbox, 13, 101 + index, UINT64_MAX) ==
		MLC_PAUSE_STEP_FATAL);
}

static void test_clock_and_delayed_sgi_failures(void)
{
	struct mlc_pause_mailbox_s mailbox;
	struct mlc_pause_saved_context_s saved = {0};

	reset(&mailbox);
	assert(mlc_pause_publish(&mailbox, 11, 3, 100, &saved) == 0);
	assert(mlc_pause_claim(&mailbox, 11, MLC_PAUSE_SERVICE_IRQ, false,
		&saved) == 0);
	assert(mlc_pause_remote_step(&mailbox, 11, 99, 195) ==
		MLC_PAUSE_STEP_FATAL);
	assert(mlc_pause_fatal_reason(&mailbox) == MLC_PAUSE_FATAL_CLOCK_INVALID);
	reset(&mailbox);
	assert(mlc_pause_publish(&mailbox, 12, 3, 100, &saved) == 0);
	assert(mlc_pause_claim(&mailbox, 12, MLC_PAUSE_SERVICE_POLL, true,
		&saved) == 0);
	assert(mlc_pause_owner_resume(&mailbox, 12) == 0);
	assert(mlc_pause_remote_step(&mailbox, 12, 101, 195) ==
		MLC_PAUSE_STEP_TERMINAL);
	assert(mlc_pause_sgi_drain(&mailbox, 12) == 0);
	assert(mlc_pause_sgi_drain(&mailbox, 12) == -EPROTO);
	assert(mlc_pause_fatal_reason(&mailbox) == MLC_PAUSE_FATAL_MAILBOX_PROTOCOL);
}

static void test_deadline_boundaries(void)
{
	struct mlc_pause_mailbox_s mailbox;
	struct mlc_pause_saved_context_s saved = {0};

	assert(MLC_PAUSE_REQUEST_TARGET_US == 10000);
	assert(MLC_PAUSE_ACCEPT_TARGET_US == 20000);
	assert(MLC_PAUSE_CANCEL_TARGET_US == 40000);
	assert(MLC_PAUSE_WORK_STOP_US == 78000);
	assert(MLC_PAUSE_RESUME_TARGET_US == 80000);
	assert(MLC_PAUSE_TERMINAL_LIMIT_US == 95000);
	reset(&mailbox);
	assert(mlc_pause_publish(&mailbox, 9, 3, 1, &saved) == 0);
	assert(mlc_pause_claim(&mailbox, 9, MLC_PAUSE_SERVICE_IRQ, false,
		&saved) == 0);
	assert(mlc_pause_remote_step(&mailbox, 9, 94999, 95000) == MLC_PAUSE_STEP_WAIT);
	assert(mlc_pause_remote_step(&mailbox, 9, 95000, 95000) == MLC_PAUSE_STEP_FATAL);
	assert(mlc_pause_fatal_reason(&mailbox) == MLC_PAUSE_FATAL_REMOTE_COUNTER_EXHAUSTED ||
		mlc_pause_fatal_reason(&mailbox) == MLC_PAUSE_FATAL_CLOCK_INVALID);
}

static void test_impossible_service_fatal(void)
{
	struct mlc_pause_mailbox_s mailbox;

	reset(&mailbox);
	mlc_pause_force_fatal(&mailbox, MLC_PAUSE_FATAL_MAILBOX_PROTOCOL);
	assert(mlc_pause_state(&mailbox) == MLC_PAUSE_FATAL);
	assert(mlc_pause_fatal_reason(&mailbox) == MLC_PAUSE_FATAL_MAILBOX_PROTOCOL);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (strcmp(argv[1], "happy") == 0) {
		test_happy_paths();
	} else if (strcmp(argv[1], "failure") == 0) {
		test_cancel_and_rejections();
		test_counter_and_token_boundaries();
		test_remote_exact_max_then_exhaustion();
		test_deadline_boundaries();
		test_clock_and_delayed_sgi_failures();
	} else if (strcmp(argv[1], "fatal") == 0) {
		test_counter_and_token_boundaries();
		test_deadline_boundaries();
		test_impossible_service_fatal();
	} else {
		return 64;
	}
	assert(g_dmb_count != 0);
	assert(g_relaxed_store_count == 0);
	puts("PASS");
	return 0;
}
