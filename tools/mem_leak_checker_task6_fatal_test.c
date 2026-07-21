#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <tinyara/clock.h>
#include <tinyara/spinlock.h>

#include "mem_leak_checker_domain.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

enum fatal_case_e {
	FATAL_HEAP_OWNERSHIP,
	FATAL_CORRUPT_UNPIN
};

struct fatal_shared_s {
	struct mlc_lifecycle_s lifecycle;
};

static struct mm_heap_s g_kernel_heap;
static bool g_critical_owned;
static clock_t g_ticks;

spinlock_t up_testset(volatile spinlock_t *lock)
{
	return __atomic_exchange_n(lock, SP_LOCKED, __ATOMIC_SEQ_CST);
}

struct mm_heap_s *kmm_get_baseheap(void)
{
	return &g_kernel_heap;
}

clock_t clock_systimer(void)
{
	return g_ticks;
}

int irq_try_enter_critical_fresh(irqstate_t *flags)
{
	if (g_critical_owned) {
		return -EALREADY;
	}
	*flags = 0;
	g_critical_owned = true;
	return 0;
}

void leave_critical_section(irqstate_t flags)
{
	(void)flags;
	assert(g_critical_owned);
	g_critical_owned = false;
}

static struct mm_loadable_domain_registration_s registration(
		struct mm_heap_s *heap, int *descriptor)
{
	struct mm_loadable_domain_registration_s domain;

	memset(&domain, 0, sizeof(domain));
	domain.slot = 1;
	domain.heap = heap;
	domain.descriptor = descriptor;
	domain.descriptor_container = descriptor;
	domain.descriptor_container_size = sizeof(*descriptor);
	domain.name = "fatal-loadable";
	domain.text_start = 0x8000;
	domain.text_size = 128;
	domain.writable_count = 1;
	domain.writable[0].start = 0x1000;
	domain.writable[0].size = 64;
	domain.writable[0].container = 0x1000;
	domain.writable[0].container_size = 64;
	return domain;
}

static void trigger_fatal(struct fatal_shared_s *shared,
		enum fatal_case_e fatal_case)
{
	struct mlc_domain_guard_s guard;

	assert(mlc_lifecycle_begin(&shared->lifecycle) == 0);
	assert(mlc_lifecycle_set_epoch(&shared->lifecycle, 1) == 0);
	assert(mlc_domain_guard_acquire(&shared->lifecycle, &guard) == 0);
	if (fatal_case == FATAL_HEAP_OWNERSHIP) {
		struct mm_heap_s *last = guard.heaps[guard.locked_heaps - 1];

		last->mm_holder++;
	} else {
		assert(guard.pin_count == 1);
		guard.pins[0].generation++;
	}
	(void)mlc_domain_guard_release(&shared->lifecycle, &guard, 0);
	_exit(2);
}

static void assert_terminal_ledger(const struct mlc_post_release_record_s *record,
		enum fatal_case_e fatal_case)
{
	assert(record->valid);
	assert(record->reason == MLC_FATAL_RESUME_AMBIGUOUS);
	assert(!record->verdict_allowed);
	if (fatal_case == FATAL_HEAP_OWNERSHIP) {
		assert(record->terminal_resources == 3);
		assert(record->terminal_ledger[0].resource == MLC_RESOURCE_DOMAIN);
		assert(record->terminal_ledger[1].resource == MLC_RESOURCE_CRITICAL);
		assert(record->terminal_ledger[2].resource == MLC_RESOURCE_HEAP);
		assert(record->released_resources == 1);
	} else {
		assert(record->terminal_resources == 1);
		assert(record->terminal_ledger[0].resource == MLC_RESOURCE_DOMAIN);
		assert(record->released_resources == 3);
	}
}

static void run_fatal_case(struct fatal_shared_s *shared,
		enum fatal_case_e fatal_case)
{
	pid_t child;
	int status;

	memset(shared, 0, sizeof(*shared));
	child = fork();
	assert(child >= 0);
	if (child == 0) {
		trigger_fatal(shared, fatal_case);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status));
	assert(WTERMSIG(status) == SIGABRT);
	assert_terminal_ledger(&shared->lifecycle.record, fatal_case);
}

static void assert_parent_admission_reusable(void)
{
	struct mlc_lifecycle_s lifecycle;

	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	mlc_lifecycle_complete(&lifecycle);
	assert(mlc_lifecycle_record(&lifecycle) != NULL);
	assert(mlc_lifecycle_begin(&lifecycle) == 0);
	mlc_lifecycle_complete(&lifecycle);
}

int main(void)
{
	struct fatal_shared_s *shared;
	struct mm_loadable_domain_registration_s domain;
	struct mm_heap_s app_heap;
	int descriptor = 1;

	shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	assert(shared != MAP_FAILED);
	g_ticks = 1;
	mm_loadable_domain_initialize();
	mm_seminitialize(&g_kernel_heap);
	mm_seminitialize(&app_heap);
	domain = registration(&app_heap, &descriptor);
	assert(mm_loadable_domain_register(&domain) == 0);
	assert(mm_loadable_domain_activate(&descriptor) == 0);

	run_fatal_case(shared, FATAL_HEAP_OWNERSHIP);
	assert_parent_admission_reusable();
	run_fatal_case(shared, FATAL_CORRUPT_UNPIN);
	assert_parent_admission_reusable();

	assert(mm_loadable_domain_disable_and_wait(&descriptor) == 0);
	assert(mm_loadable_domain_finish_unload(&descriptor) == 0);
	assert(munmap(shared, sizeof(*shared)) == 0);
	printf("MLC_TASK6_FATAL status=PASS cases=2 signal=SIGABRT "
		"shared_postmortem=true parent_admission_reusable=true "
		"child_cleanup_claim=false\n");
	return 0;
}
