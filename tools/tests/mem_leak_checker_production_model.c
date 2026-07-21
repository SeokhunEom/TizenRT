#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mem_leak_checker_pause.h"
#include "mem_leak_checker_pause_owner.h"
#include "mem_leak_checker_lifecycle.h"

#include "mem_leak_checker_pause_service.c"

static volatile int g_send_result;
static volatile bool g_service_entered;
static volatile int g_fatal_count;
static volatile uint64_t g_clock = 100;
static volatile int g_service_kind;
static pthread_t g_service_thread;
static pthread_t g_main_thread;
static uint32_t g_frame[18];
static struct tcb_s g_tcb;
static struct mlc_budget_counters_s g_budget;

static void *service_irq_thread(void *arg);
static void *service_poll_thread(void *arg);

struct service_token_call_s {
	uint32_t token;
};

static void *service_token_thread(void *arg)
{
	const struct service_token_call_s *call = arg;

	__atomic_store_n(&g_service_entered, true, __ATOMIC_RELEASE);
	(void)mlc_pause_service_irq(1, 44, g_frame, call->token);
	return NULL;
}

clock_t clock_systimer(void)
{
	return (clock_t)g_clock;
}

uint64_t up_mem_leak_monotonic_usec(void)
{
	if (mlc_pause_state(&g_mlc_pause_mailbox[1]) == MLC_PAUSE_PAUSED_IRQ ||
		mlc_pause_state(&g_mlc_pause_mailbox[1]) == MLC_PAUSE_PAUSED_POLL) {
		if (!pthread_equal(pthread_self(), g_main_thread)) {
			while (mlc_pause_state(&g_mlc_pause_mailbox[1]) ==
				MLC_PAUSE_PAUSED_IRQ ||
				mlc_pause_state(&g_mlc_pause_mailbox[1]) ==
				MLC_PAUSE_PAUSED_POLL) {
				sched_yield();
			}
		}
		return __atomic_load_n(&g_clock, __ATOMIC_RELAXED);
	}
	sched_yield();
	return __atomic_fetch_add(&g_clock, 1, __ATOMIC_RELAXED);
}

int this_cpu(void)
{
	return 0;
}

struct tcb_s *current_task(int cpu)
{
	assert(cpu == 1);
	g_service_entered = true;
	return &g_tcb;
}

void up_mem_leak_capture_current(struct up_mem_leak_capture_s *capture)
{
	memset(capture, 0, sizeof(*capture));
	capture->magic = UP_MEM_LEAK_CAPTURE_MAGIC;
	capture->version = UP_MEM_LEAK_CAPTURE_VERSION;
	capture->words = UP_MEM_LEAK_CAPTURE_WORDS;
	capture->flags = UP_MEM_LEAK_CAPTURE_FLAG_TASK;
}

int up_mem_leak_pause_request(int cpu)
{
	int result;

	assert(cpu == 1);
	if (g_send_result < 0) {
		return g_send_result;
	}
	__atomic_store_n(&g_service_entered, false, __ATOMIC_RELEASE);
	result = pthread_create(&g_service_thread, NULL,
		g_service_kind == MLC_PAUSE_SERVICE_POLL ? service_poll_thread :
		service_irq_thread, NULL);
	if (result == 0) {
		while (!__atomic_load_n(&g_service_entered, __ATOMIC_ACQUIRE)) {
			sched_yield();
		}
		while (mlc_pause_state(&g_mlc_pause_mailbox[1]) ==
			MLC_PAUSE_PAUSE_REQ) {
			sched_yield();
		}
	}
	return result;
}

void mlc_pause_fatal_dispatch(enum mlc_fatal_reason_e reason)
{
	(void)reason;
	g_fatal_count++;
}

static void *service_irq_thread(void *arg)
{
	(void)arg;
	__atomic_store_n(&g_service_entered, true, __ATOMIC_RELEASE);
	(void)mlc_pause_service_irq(1, 42, g_frame,
		mlc_pause_service_token(1));
	return NULL;
}

static void *service_poll_thread(void *arg)
{
	(void)arg;
	__atomic_store_n(&g_service_entered, true, __ATOMIC_RELEASE);
	(void)mlc_pause_service_poll(1, 1);
	return NULL;
}

static void start_service_thread(void)
{
	while (mlc_pause_state(&g_mlc_pause_mailbox[1]) != MLC_PAUSE_PAUSED_IRQ &&
		mlc_pause_state(&g_mlc_pause_mailbox[1]) != MLC_PAUSE_PAUSED_POLL) {
		sched_yield();
	}
}

static void test_sgi_failure_recycles_published_state(void)
{
	g_send_result = -EIO;
	assert(mlc_pause_owner_request_cpu(1, 7, 100) == -EIO);
	assert(mlc_pause_state(&g_mlc_pause_mailbox[1]) == MLC_PAUSE_IDLE);
	assert(__atomic_load_n(&g_mlc_pause_mailbox[1].token,
		__ATOMIC_ACQUIRE) == 0);
	assert(__atomic_load_n(&g_mlc_pause_mailbox[1].request_pending,
		__ATOMIC_ACQUIRE) == 0);
	assert(__atomic_load_n(&g_mlc_pause_mailbox[1].sgi_outstanding,
		__ATOMIC_ACQUIRE) == 0);
}

static void test_frame_decode_and_poll_drain(void)
{
	int result;

	for (size_t index = 0; index < 18; index++) {
		g_frame[index] = (uint32_t)(0x1000 + index);
	}
	g_send_result = 0;
	g_service_kind = MLC_PAUSE_SERVICE_IRQ;
	assert(mlc_pause_owner_request_cpu(1, 8, 100) == 0);
	start_service_thread();
	assert(mlc_pause_owner_resume_cpu(1, 8) == 0);
	result = pthread_join(g_service_thread, NULL);
	assert(result == 0);
	assert(g_mlc_pause_mailbox[1].saved_context.callee_saved[0] ==
		g_frame[REG_R4]);
	assert(g_mlc_pause_mailbox[1].saved_context.stack_pointer ==
		g_frame[REG_SP]);
	assert(g_mlc_pause_mailbox[1].saved_context.status == g_frame[REG_CPSR]);
	assert(g_mlc_pause_mailbox[1].saved_context.exception == 42);
	assert(mlc_pause_owner_recycle_cpu(1, 8) == 0);

	g_service_kind = MLC_PAUSE_SERVICE_POLL;
	assert(mlc_pause_owner_request_cpu(1, 9, 100) == 0);
	start_service_thread();
	assert(mlc_pause_owner_resume_cpu(1, 9) == 0);
	assert(pthread_join(g_service_thread, NULL) == 0);
	assert(mlc_pause_service_irq(1, 43, g_frame,
		mlc_pause_service_token(1)));
	assert(mlc_pause_service_irq(1, 43, g_frame,
		mlc_pause_service_token(1)));
	assert(g_fatal_count == 1);
	assert(mlc_pause_state(&g_mlc_pause_mailbox[1]) == MLC_PAUSE_FATAL);
	mlc_pause_mailbox_init(&g_mlc_pause_mailbox[1]);
}

static void test_cancel_routes_and_late_resume(void)
{
	struct mlc_pause_saved_context_s saved = {0};
	struct mlc_pause_owner_s owner;

	mlc_pause_mailbox_init(&g_mlc_pause_mailbox[1]);
	assert(mlc_pause_publish(&g_mlc_pause_mailbox[1], 20, 3, 100,
		&saved) == 0);
	assert(mlc_pause_owner_cancel_cpu(1, 20) == 0);
	assert(mlc_pause_state(&g_mlc_pause_mailbox[1]) ==
		MLC_PAUSE_CANCEL_REQ_UNCLAIMED);
	assert(mlc_pause_service_irq(1, 40, g_frame, 20));
	assert(mlc_pause_state(&g_mlc_pause_mailbox[1]) == MLC_PAUSE_CANCELLED);
	assert(mlc_pause_owner_recycle_cpu(1, 20) == 0);

	mlc_pause_mailbox_init(&g_mlc_pause_mailbox[1]);
	assert(mlc_pause_publish(&g_mlc_pause_mailbox[1], 21, 3, 100,
		&saved) == 0);
	{
		uint32_t expected = MLC_PAUSE_PAUSE_REQ;
		assert(__atomic_compare_exchange_n(&g_mlc_pause_mailbox[1].state,
			&expected, MLC_PAUSE_CLAIMED_IRQ, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE));
	}
	__atomic_store_n(&g_mlc_pause_mailbox[1].service_path,
		MLC_PAUSE_SERVICE_IRQ, __ATOMIC_RELEASE);
	__atomic_store_n(&g_mlc_pause_mailbox[1].request_pending, 0,
		__ATOMIC_RELEASE);
	__atomic_store_n(&g_mlc_pause_mailbox[1].sgi_outstanding, 0,
		__ATOMIC_RELEASE);
	assert(mlc_pause_owner_cancel_cpu(1, 21) == 0);
	assert(mlc_pause_state(&g_mlc_pause_mailbox[1]) ==
		MLC_PAUSE_CANCEL_REQ_IRQ);
	assert(mlc_pause_remote_step(&g_mlc_pause_mailbox[1], 21, 101, 195) ==
		MLC_PAUSE_STEP_TERMINAL);
	assert(mlc_pause_state(&g_mlc_pause_mailbox[1]) == MLC_PAUSE_CANCELLED);
	assert(mlc_pause_owner_recycle_cpu(1, 21) == 0);

	g_clock = 100;
	g_send_result = 0;
	g_service_kind = MLC_PAUSE_SERVICE_IRQ;
	assert(mlc_pause_owner_begin(&owner, 100) == 0);
	start_service_thread();
	g_clock = 80100;
	mlc_pause_owner_cleanup(&owner);
	assert(owner.error == -ETIMEDOUT);
	assert(pthread_join(g_service_thread, NULL) == 0);
	assert(mlc_pause_state(&g_mlc_pause_mailbox[1]) == MLC_PAUSE_IDLE);
}

static void test_delayed_irq_rejected_across_recycle(void)
{
	struct mlc_pause_saved_context_s saved = {0};
	struct service_token_call_s call;

	mlc_pause_mailbox_init(&g_mlc_pause_mailbox[1]);
	assert(mlc_pause_publish(&g_mlc_pause_mailbox[1], 31, 3, 100,
		&saved) == 0);
	call.token = 31;
	assert(pthread_create(&g_service_thread, NULL, service_token_thread,
		&call) == 0);
	start_service_thread();
	assert(mlc_pause_owner_resume_cpu(1, 31) == 0);
	assert(pthread_join(g_service_thread, NULL) == 0);
	assert(mlc_pause_owner_recycle_cpu(1, 31) == 0);

	assert(mlc_pause_publish(&g_mlc_pause_mailbox[1], 32, 3, 100,
		&saved) == 0);
	assert(!mlc_pause_service_irq(1, 45, g_frame, 31));
	assert(mlc_pause_state(&g_mlc_pause_mailbox[1]) == MLC_PAUSE_PAUSE_REQ);
	assert(__atomic_load_n(&g_mlc_pause_mailbox[1].request_pending,
		__ATOMIC_ACQUIRE) == 1);
	call.token = 32;
	assert(pthread_create(&g_service_thread, NULL, service_token_thread,
		&call) == 0);
	start_service_thread();
	assert(mlc_pause_owner_resume_cpu(1, 32) == 0);
	assert(pthread_join(g_service_thread, NULL) == 0);
	assert(mlc_pause_owner_recycle_cpu(1, 32) == 0);
}

static void test_service_deadline_overflow_fails_closed(void)
{
	struct mlc_pause_saved_context_s saved = {0};
	struct service_token_call_s call = {.token = 33};
	int fatal_before = g_fatal_count;

	mlc_pause_mailbox_init(&g_mlc_pause_mailbox[1]);
	assert(mlc_pause_publish(&g_mlc_pause_mailbox[1], call.token, 3,
		UINT64_MAX, &saved) == 0);
	assert(pthread_create(&g_service_thread, NULL, service_token_thread,
		&call) == 0);
	assert(pthread_join(g_service_thread, NULL) == 0);
	assert(g_fatal_count == fatal_before + 1);
	assert(mlc_pause_state(&g_mlc_pause_mailbox[1]) == MLC_PAUSE_FATAL);
	assert(mlc_pause_fatal_reason(&g_mlc_pause_mailbox[1]) ==
		MLC_PAUSE_FATAL_CLOCK_INVALID);
}

static void cleanup_domain(void *arg)
{
	(*(int *)arg) = (*(int *)arg) * 10 + 1;
}

static void cleanup_critical(void *arg)
{
	(*(int *)arg) = (*(int *)arg) * 10 + 2;
}

static void cleanup_heap(void *arg)
{
	(*(int *)arg) = (*(int *)arg) * 10 + 3;
}

static void test_lifecycle_unwinds_pause_last(void)
{
	struct mlc_lifecycle_s lifecycle;
	struct mlc_pause_owner_s owner;
	int cleanup_order = 0;
	uint64_t epoch_usec = __atomic_load_n(&g_clock, __ATOMIC_RELAXED);

	mlc_pause_mailbox_init(&g_mlc_pause_mailbox[1]);
	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	assert(mlc_lifecycle_set_epoch(&lifecycle, epoch_usec) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_DOMAIN,
		MLC_RESOURCE_DOMAIN, cleanup_domain, &cleanup_order) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_CRITICAL,
		MLC_RESOURCE_CRITICAL, cleanup_critical, &cleanup_order) == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_HEAP, cleanup_heap, &cleanup_order) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_CAPTURED) == 0);
	g_send_result = 0;
	g_service_kind = MLC_PAUSE_SERVICE_IRQ;
	int owner_result = mlc_pause_owner_begin(&owner, lifecycle.epoch_usec);
	assert(owner_result == 0);
	assert(mlc_lifecycle_push(&lifecycle, MLC_PHASE_PAUSED,
		MLC_RESOURCE_PAUSE, mlc_pause_owner_cleanup, &owner) == 0);
	assert(mlc_lifecycle_advance(&lifecycle, MLC_PHASE_ANALYSIS) == 0);
	mlc_lifecycle_complete(&lifecycle);
	assert(pthread_join(g_service_thread, NULL) == 0);
	assert(cleanup_order == 321);
	assert(lifecycle.count == 0);
	assert(mlc_lifecycle_record(&lifecycle)->terminal_resources == 4);
}

int main(void)
{
	g_main_thread = pthread_self();
	assert(mlc_budget_counters_init(&g_budget) == 0);
	mlc_budget_bind(&g_budget);
	test_sgi_failure_recycles_published_state();
	test_frame_decode_and_poll_drain();
	test_cancel_routes_and_late_resume();
	test_delayed_irq_rejected_across_recycle();
	test_service_deadline_overflow_fails_closed();
	test_lifecycle_unwinds_pause_last();
	puts("PASS");
	return 0;
}
