#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "mem_leak_checker_pause.h"
#include "mem_leak_checker_pause_owner.h"

static uint64_t g_times[16];
static size_t g_time_count;
static size_t g_time_index;
static enum mlc_pause_state_e g_state;
static enum mlc_pause_state_e g_initial_state = MLC_PAUSE_PAUSED_IRQ;
static bool g_drained;
static bool g_auto_terminal;
static enum mlc_fatal_reason_e g_fatal;
static jmp_buf g_fatal_jump;

static void set_times(const uint64_t *times, size_t count)
{
	assert(count <= sizeof(g_times) / sizeof(g_times[0]));
	for (size_t index = 0; index < count; index++) {
		g_times[index] = times[index];
	}
	g_time_count = count;
	g_time_index = 0;
	g_state = g_initial_state;
	g_drained = false;
	g_auto_terminal = true;
	g_fatal = MLC_PAUSE_FATAL_NONE;
}

uint64_t up_mem_leak_monotonic_usec(void)
{
	assert(g_time_count != 0);
	return g_times[g_time_index < g_time_count ? g_time_index++ :
		g_time_count - 1];
}

int this_cpu(void)
{
	return 0;
}

int mlc_pause_owner_request_cpu(int cpu, uint32_t token, uint64_t epoch_usec)
{
	assert(cpu == 1 && token != 0 && epoch_usec != 0);
	return 0;
}

enum mlc_pause_state_e mlc_pause_owner_state_cpu(int cpu, uint32_t token)
{
	assert(cpu == 1 && token != 0);
	return g_state;
}

int mlc_pause_owner_cancel_cpu(int cpu, uint32_t token)
{
	assert(cpu == 1 && token != 0);
	g_state = g_auto_terminal ? MLC_PAUSE_CANCELLED :
		MLC_PAUSE_CANCEL_REQ_IRQ;
	g_drained = g_auto_terminal;
	return 0;
}

int mlc_pause_owner_resume_cpu(int cpu, uint32_t token)
{
	assert(cpu == 1 && token != 0);
	g_state = g_auto_terminal ? MLC_PAUSE_RESUMED :
		MLC_PAUSE_RESUME_REQ_IRQ;
	g_drained = g_auto_terminal;
	return 0;
}

int mlc_pause_owner_recycle_cpu(int cpu, uint32_t token)
{
	assert(cpu == 1 && token != 0 && g_drained);
	g_state = MLC_PAUSE_IDLE;
	return 0;
}

bool mlc_pause_owner_drained_cpu(int cpu, uint32_t token)
{
	assert(cpu == 1 && token != 0);
	return g_drained;
}

void mlc_pause_fatal_dispatch(enum mlc_fatal_reason_e reason)
{
	g_fatal = reason;
	longjmp(g_fatal_jump, 1);
}

static void test_request_and_accept_boundaries(void)
{
	struct mlc_pause_owner_s owner;
	const uint64_t request_exact[] = {10100, 20100};
	const uint64_t request_late[] = {10101};
	const uint64_t accept_exact[] = {100, 20100};
	const uint64_t accept_late[] = {100, 20101, 20200};

	set_times(request_exact, 2);
	assert(mlc_pause_owner_begin(&owner, 100) == 0);
	mlc_pause_owner_cleanup(&owner);
	assert(owner.error == 0);
	set_times(request_late, 1);
	assert(mlc_pause_owner_begin(&owner, 100) == -ETIMEDOUT);
	set_times(accept_exact, 2);
	assert(mlc_pause_owner_begin(&owner, 100) == 0);
	set_times((const uint64_t[]){20101}, 1);
	mlc_pause_owner_cleanup(&owner);
	g_initial_state = MLC_PAUSE_PAUSE_REQ;
	set_times(accept_late, 3);
	assert(mlc_pause_owner_begin(&owner, 100) == -ETIMEDOUT);
	g_initial_state = MLC_PAUSE_PAUSED_IRQ;
}

static void test_work_and_resume_boundaries(void)
{
	struct mlc_pause_owner_s owner;
	const uint64_t begin[] = {100, 101, 102};

	set_times(begin, 3);
	assert(mlc_pause_owner_begin(&owner, 100) == 0);
	set_times((const uint64_t[]){78099}, 1);
	assert(mlc_pause_owner_work_allowed(&owner));
	set_times((const uint64_t[]){80099}, 1);
	mlc_pause_owner_cleanup(&owner);
	assert(owner.error == 0);
	set_times(begin, 3);
	assert(mlc_pause_owner_begin(&owner, 100) == 0);
	set_times((const uint64_t[]){78100}, 1);
	assert(!mlc_pause_owner_work_allowed(&owner));
	set_times((const uint64_t[]){80100}, 1);
	mlc_pause_owner_cleanup(&owner);
	assert(owner.error == -ETIMEDOUT);
}

static void test_invalid_clocks_fatal(void)
{
	struct mlc_pause_owner_s owner;

	set_times((const uint64_t[]){100, 99}, 2);
	if (setjmp(g_fatal_jump) == 0) {
		(void)mlc_pause_owner_begin(&owner, 100);
		assert(false);
	}
	assert(g_fatal == MLC_PAUSE_FATAL_CLOCK_INVALID);
	set_times((const uint64_t[]){100}, 1);
	g_state = MLC_PAUSE_PAUSE_REQ;
	if (setjmp(g_fatal_jump) == 0) {
		(void)mlc_pause_owner_begin(&owner, 100);
		assert(false);
	}
	assert(g_fatal == MLC_PAUSE_FATAL_CLOCK_INVALID);
}

static void test_terminal_deadlines_fatal(void)
{
	struct mlc_pause_owner_s owner;

	set_times((const uint64_t[]){100, 101, 20100, 40100}, 4);
	g_state = MLC_PAUSE_PAUSE_REQ;
	g_auto_terminal = false;
	if (setjmp(g_fatal_jump) == 0) {
		(void)mlc_pause_owner_begin(&owner, 100);
		assert(false);
	}
	assert(g_fatal == MLC_PAUSE_FATAL_CANCEL_AMBIGUOUS);
	set_times((const uint64_t[]){100, 101, 102}, 3);
	assert(mlc_pause_owner_begin(&owner, 100) == 0);
	set_times((const uint64_t[]){95100}, 1);
	g_auto_terminal = false;
	if (setjmp(g_fatal_jump) == 0) {
		mlc_pause_owner_cleanup(&owner);
		assert(false);
	}
	assert(g_fatal == MLC_PAUSE_FATAL_RESUME_AMBIGUOUS);
}

static void test_deadline_overflow_fatal(void)
{
	struct mlc_pause_owner_s owner;
	uint64_t deadline = 0;

	assert(!mlc_pause_deadline_after(UINT64_MAX, 1, &deadline));
	assert(!mlc_pause_deadline_after(UINT64_MAX - 1, 2, &deadline));
	set_times((const uint64_t[]){UINT64_MAX}, 1);
	if (setjmp(g_fatal_jump) == 0) {
		(void)mlc_pause_owner_begin(&owner, UINT64_MAX);
		assert(false);
	}
	assert(g_fatal == MLC_PAUSE_FATAL_CLOCK_INVALID);
}

int main(void)
{
	test_request_and_accept_boundaries();
	test_work_and_resume_boundaries();
	test_invalid_clocks_fatal();
	test_terminal_deadlines_fatal();
	test_deadline_overflow_fatal();
	puts("PASS");
	return 0;
}
