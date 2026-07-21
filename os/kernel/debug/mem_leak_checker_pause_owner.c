#include <tinyara/config.h>

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <tinyara/arch.h>

#include "mem_leak_checker_pause.h"
#include "mem_leak_checker_pause_owner.h"

#if defined(CONFIG_SMP) && CONFIG_SMP_NCPUS == 2
static volatile uint32_t g_mlc_pause_token;

static void owner_fatal(enum mlc_fatal_reason_e reason)
	__attribute__((noreturn));

static void owner_fatal(enum mlc_fatal_reason_e reason)
{
	mlc_pause_fatal_dispatch(reason);
	__builtin_unreachable();
}

static int next_token(uint32_t *token)
{
	uint32_t current = __atomic_load_n(&g_mlc_pause_token, __ATOMIC_ACQUIRE);

	for (;;) {
		uint32_t next;

		if (current == UINT32_MAX) {
			return -EOVERFLOW;
		}
		next = current + 1;
		if (__atomic_compare_exchange_n(&g_mlc_pause_token, &current, next,
			false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			*token = next;
			return 0;
		}
	}
}

static uint64_t owner_now(struct mlc_pause_owner_s *owner)
{
	uint64_t now = up_mem_leak_monotonic_usec();

	if (now == 0 || now < owner->last_usec) {
		owner_fatal(MLC_PAUSE_FATAL_CLOCK_INVALID);
	}
	if (now == owner->last_usec) {
		owner->stagnant_polls++;
		if (owner->stagnant_polls > CONFIG_MEM_LEAK_REMOTE_PAUSED_MAX_POLLS) {
			owner_fatal(MLC_PAUSE_FATAL_CLOCK_INVALID);
		}
	} else {
		owner->stagnant_polls = 0;
	}
	owner->last_usec = now;
	return now;
}

static struct mlc_budget_counters_s *owner_budget(
		struct mlc_pause_owner_s *owner)
{
	return owner->shared_budget != NULL ? owner->shared_budget : &owner->budget;
}

static bool state_is_paused(enum mlc_pause_state_e state)
{
	return state == MLC_PAUSE_PAUSED_IRQ || state == MLC_PAUSE_PAUSED_POLL;
}

static bool state_is_terminal(enum mlc_pause_state_e state)
{
	return state == MLC_PAUSE_CANCELLED || state == MLC_PAUSE_RESUMED;
}

static int cancel_before_pause(struct mlc_pause_owner_s *owner);

static int wait_terminal(struct mlc_pause_owner_s *owner, uint64_t limit,
		enum mlc_fatal_reason_e reason,
		enum mlc_budget_counter_e counter)
{
	uint32_t polls = CONFIG_MEM_LEAK_REMOTE_PAUSED_MAX_POLLS;
	bool resume_completion = counter == MLC_BUDGET_RESUME_COMPLETION;

	while (polls-- > 0) {
		uint64_t now = up_mem_leak_monotonic_usec();
		int begin_ret = resume_completion ?
			mlc_budget_chunk_begin_resume(owner_budget(owner), counter, 1, now) :
			mlc_budget_chunk_begin(owner_budget(owner), counter, 1, now);
		uint64_t end_now = up_mem_leak_monotonic_usec();
		int end_ret = resume_completion ?
			mlc_budget_chunk_end_resume(owner_budget(owner), end_now) :
			mlc_budget_chunk_end(owner_budget(owner), end_now);

		if (begin_ret < 0 || end_ret < 0) {
			owner_fatal(reason);
		}
		enum mlc_pause_state_e state = mlc_pause_owner_state_cpu(
			owner->remote_cpu, owner->token);

		if (state_is_terminal(state) && mlc_pause_owner_drained_cpu(
			owner->remote_cpu, owner->token)) {
			return mlc_pause_owner_recycle_cpu(owner->remote_cpu,
				owner->token);
		}
		if (state == MLC_PAUSE_FATAL || owner_now(owner) >= limit) {
			owner_fatal(reason);
		}
	}
	owner_fatal(MLC_PAUSE_FATAL_CLOCK_INVALID);
}

static int cancel_before_pause(struct mlc_pause_owner_s *owner)
{
	uint64_t deadline;

	if (!mlc_pause_deadline_after(owner->epoch_usec,
		MLC_PAUSE_CANCEL_TARGET_US, &deadline)) {
		owner_fatal(MLC_PAUSE_FATAL_CLOCK_INVALID);
	}
	if (mlc_pause_owner_cancel_cpu(owner->remote_cpu, owner->token) < 0) {
		owner_fatal(MLC_PAUSE_FATAL_CANCEL_AMBIGUOUS);
	}
	return wait_terminal(owner, deadline, MLC_PAUSE_FATAL_CANCEL_AMBIGUOUS,
		MLC_BUDGET_CANCEL_COMPLETION);
}

static int finish_pause(struct mlc_pause_owner_s *owner)
{
	uint64_t now;
	uint64_t terminal_deadline;
	uint64_t resume_deadline;
	bool resume_late;
	int result;

	if (!owner->requested) {
		return 0;
	}
	if (!owner->active) {
		return cancel_before_pause(owner);
	}
	if (!mlc_pause_deadline_after(owner->epoch_usec,
		MLC_PAUSE_TERMINAL_LIMIT_US, &terminal_deadline) ||
		!mlc_pause_deadline_after(owner->epoch_usec,
		MLC_PAUSE_RESUME_TARGET_US, &resume_deadline)) {
		owner_fatal(MLC_PAUSE_FATAL_CLOCK_INVALID);
	}
	now = owner_now(owner);
	if (now >= terminal_deadline) {
		owner_fatal(MLC_PAUSE_FATAL_RESUME_AMBIGUOUS);
	}
	resume_late = now >= resume_deadline;
	if (mlc_pause_owner_resume_cpu(owner->remote_cpu, owner->token) < 0) {
		owner_fatal(MLC_PAUSE_FATAL_RESUME_AMBIGUOUS);
	}
	owner->active = false;
	result = wait_terminal(owner, terminal_deadline,
		MLC_PAUSE_FATAL_RESUME_AMBIGUOUS, MLC_BUDGET_RESUME_COMPLETION);
	return result < 0 ? result : (resume_late ? -ETIMEDOUT : 0);
}

static int mlc_pause_owner_begin_internal(struct mlc_pause_owner_s *owner,
		uint64_t epoch_usec, struct mlc_budget_counters_s *shared_budget)
{
	uint32_t polls = CONFIG_MEM_LEAK_REMOTE_PAUSED_MAX_POLLS;
	uint64_t request_deadline;
	uint64_t accept_deadline;
	int result;

	if (owner == NULL) {
		return -EINVAL;
	}
	memset(owner, 0, sizeof(*owner));
	owner->shared_budget = shared_budget;
	if (shared_budget == NULL && mlc_budget_counters_init(&owner->budget) < 0) {
		return -ERANGE;
	}
	if (epoch_usec == 0) {
		return -EINVAL;
	}
	if (!mlc_pause_deadline_after(epoch_usec, MLC_PAUSE_REQUEST_TARGET_US,
		&request_deadline) || !mlc_pause_deadline_after(epoch_usec,
		MLC_PAUSE_ACCEPT_TARGET_US, &accept_deadline)) {
		owner_fatal(MLC_PAUSE_FATAL_CLOCK_INVALID);
	}
	owner->epoch_usec = epoch_usec;
	owner->last_usec = owner->epoch_usec;
	owner->remote_cpu = 1 - this_cpu();
	result = next_token(&owner->token);
	if (result < 0) {
		return result;
	}
	if (owner_now(owner) > request_deadline) {
		return -ETIMEDOUT;
	}
	result = mlc_pause_owner_request_cpu(owner->remote_cpu, owner->token,
		owner->epoch_usec);
	if (result < 0) {
		return result;
	}
	owner->requested = true;
	while (polls-- > 0) {
		if (mlc_budget_chunk_begin(owner_budget(owner), MLC_BUDGET_PAUSE_ACK,
			1, up_mem_leak_monotonic_usec()) < 0 ||
			mlc_budget_chunk_end(owner_budget(owner),
			up_mem_leak_monotonic_usec()) < 0) {
			if (cancel_before_pause(owner) < 0) {
				owner_fatal(MLC_PAUSE_FATAL_CANCEL_AMBIGUOUS);
			}
			owner->active = false;
			owner->requested = false;
			return -E2BIG;
		}
		enum mlc_pause_state_e state = mlc_pause_owner_state_cpu(
			owner->remote_cpu, owner->token);

		if (state_is_paused(state)) {
			if (owner_now(owner) > accept_deadline) {
				cancel_before_pause(owner);
				owner->requested = false;
				return -ETIMEDOUT;
			}
			owner->active = true;
			return 0;
		}
		if (state == MLC_PAUSE_FATAL) {
			owner_fatal(MLC_PAUSE_FATAL_CANCEL_AMBIGUOUS);
		}
		if (owner_now(owner) >= accept_deadline) {
			cancel_before_pause(owner);
			owner->requested = false;
			return -ETIMEDOUT;
		}
	}
	owner_fatal(MLC_PAUSE_FATAL_CLOCK_INVALID);
}

int mlc_pause_owner_begin(struct mlc_pause_owner_s *owner,
		uint64_t epoch_usec)
{
	return mlc_pause_owner_begin_internal(owner, epoch_usec, NULL);
}

int mlc_pause_owner_begin_with_budget(struct mlc_pause_owner_s *owner,
		uint64_t epoch_usec, struct mlc_budget_counters_s *budget)
{
	if (budget == NULL) {
		return -EINVAL;
	}
	return mlc_pause_owner_begin_internal(owner, epoch_usec, budget);
}

bool mlc_pause_owner_work_allowed(struct mlc_pause_owner_s *owner)
{
	uint64_t work_deadline;

	if (owner == NULL || !owner->active) {
		return owner != NULL && !owner->requested;
	}
	if (!mlc_pause_deadline_after(owner->epoch_usec,
		MLC_PAUSE_WORK_STOP_US, &work_deadline)) {
		owner_fatal(MLC_PAUSE_FATAL_CLOCK_INVALID);
	}
	return owner_now(owner) < work_deadline;
}

void mlc_pause_owner_cleanup(void *arg)
{
	struct mlc_pause_owner_s *owner = arg;

	owner->error = finish_pause(owner);
	owner->requested = false;
	owner->active = false;
}
#else
int mlc_pause_owner_begin(struct mlc_pause_owner_s *owner,
		uint64_t epoch_usec)
{
	if (owner == NULL) {
		return -EINVAL;
	}
	memset(owner, 0, sizeof(*owner));
	(void)epoch_usec;
	return 0;
}

int mlc_pause_owner_begin_with_budget(struct mlc_pause_owner_s *owner,
		uint64_t epoch_usec, struct mlc_budget_counters_s *budget)
{
	if (owner == NULL || budget == NULL) {
		return -EINVAL;
	}
	return mlc_pause_owner_begin(owner, epoch_usec);
}

bool mlc_pause_owner_work_allowed(struct mlc_pause_owner_s *owner)
{
	return owner != NULL;
}

void mlc_pause_owner_cleanup(void *arg)
{
	(void)arg;
}
#endif
