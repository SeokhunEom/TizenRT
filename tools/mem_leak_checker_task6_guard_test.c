#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tinyara/clock.h>
#include <tinyara/spinlock.h>

#include "mem_leak_checker_domain.h"
#include "mm_loadable_domain_internal.h"

static struct mm_heap_s g_kernel_heap;
static bool g_critical_owned;
static unsigned int g_irq_waitlock;
static clock_t g_ticks;
static clock_t g_clock_values[12];
static size_t g_clock_count;
static size_t g_clock_index;

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
	if (g_clock_index < g_clock_count) {
		return g_clock_values[g_clock_index++];
	}
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
	struct mm_loadable_domain_registration_s value;

	memset(&value, 0, sizeof(value));
	value.slot = 1;
	value.heap = heap;
	value.descriptor = descriptor;
	value.descriptor_container = descriptor;
	value.descriptor_container_size = sizeof(*descriptor);
	value.name = "loadable";
	value.text_start = 0x8000;
	value.text_size = 128;
	value.writable_count = 1;
	value.writable[0].start = 0x1000;
	value.writable[0].size = 64;
	value.writable[0].container = 0x1000;
	value.writable[0].container_size = 64;
	return value;
}

static void reset_lifecycle(struct mlc_lifecycle_s *lifecycle)
{
	memset(lifecycle, 0, sizeof(*lifecycle));
	lifecycle->admitted = true;
	lifecycle->phase = MLC_PHASE_WORKSPACE;
	lifecycle->epoch_usec = 1;
	assert(mlc_budget_counters_init(&lifecycle->counters) == 0);
	assert(mlc_budget_set_epoch(&lifecycle->counters,
		lifecycle->epoch_usec, 78000, 95000) == 0);
	g_ticks = 1;
	g_clock_count = 0;
	g_clock_index = 0;
	g_irq_waitlock = 0;
}

static void set_clock_values(clock_t first, clock_t second, clock_t third)
{
	g_clock_values[0] = first;
	g_clock_values[1] = second;
	g_clock_values[2] = third;
	g_clock_count = 3;
	g_clock_index = 0;
}

static void set_heap_state(struct mm_heap_s *heap, pid_t holder, int count,
		int semcount)
{
	heap->mm_holder = holder;
	heap->mm_counts_held = count;
	heap->mm_semaphore.semcount = semcount;
}

int main(int argc, char **argv)
{
	struct mlc_lifecycle_s lifecycle;
	struct mlc_domain_guard_s guard;
	struct mm_loadable_domain_registration_s domain;
	struct mm_heap_s app_heap;
	int descriptor = 1;
	int repeat = argc == 2 ? atoi(argv[1]) : 1;
	int iteration;
	pid_t self = getpid();

	assert(argc <= 2 && repeat > 0);

	mm_loadable_domain_initialize();
	mm_seminitialize(&g_kernel_heap);
	mm_seminitialize(&app_heap);
	domain = registration(&app_heap, &descriptor);
	assert(mm_loadable_domain_register(&domain) == 0);
	assert(mm_loadable_domain_activate(&descriptor) == 0);

	reset_lifecycle(&lifecycle);
	assert(mlc_domain_guard_acquire(&lifecycle, &guard) == 0);
	assert(guard.pin_count == 1 && guard.heap_count == 2 &&
		guard.locked_heaps == 2);
	assert(g_kernel_heap.mm_holder == self && app_heap.mm_holder == self);
	assert(mlc_domain_guard_find_pin(&guard, "loadable") == &guard.pins[0]);
	assert(mlc_domain_guard_release(&lifecycle, &guard, 0) == 0);
	assert(guard.pin_count == 0 && guard.report_pin_count == 1);
	assert(mlc_domain_guard_find_pin(&guard, "loadable") == &guard.pins[0]);
	assert(guard.pins[0].text_start == 0x8000);
	assert(!g_critical_owned && g_irq_waitlock == 0);
	assert(g_kernel_heap.mm_holder == -1 && app_heap.mm_holder == -1);
	reset_lifecycle(&lifecycle);
	assert(mlc_domain_guard_acquire(&lifecycle, &guard) == 0);
	assert(mm_loadable_domain_lock(true) == 0);
	assert(mlc_domain_guard_release(&lifecycle, &guard, 0) == 0);
	mm_loadable_domain_unlock();

	set_heap_state(&app_heap, self + 1, 1, 0);
	reset_lifecycle(&lifecycle);
	set_clock_values(1, 10000, 10001);
	assert(mlc_domain_guard_acquire(&lifecycle, &guard) == -EBUSY);
	assert(guard.attempt_count == 2 && guard.elapsed_usec == 9999);
	reset_lifecycle(&lifecycle);
	set_clock_values(1, 10001, 10002);
	assert(mlc_domain_guard_acquire(&lifecycle, &guard) == -EBUSY);
	assert(guard.attempt_count == 1 && guard.elapsed_usec == 0);
	for (iteration = 0; iteration < repeat; iteration++) {
		reset_lifecycle(&lifecycle);
		assert(mlc_domain_guard_acquire(&lifecycle, &guard) == -EBUSY);
		assert(guard.attempt_count == 8);
	}
	assert(lifecycle.count == 0 && !g_critical_owned);
	assert(g_kernel_heap.mm_holder == -1 && g_kernel_heap.mm_counts_held == 0);

	for (iteration = 0; iteration < repeat; iteration++) {
		set_heap_state(&app_heap, -1, 0, 2);
		reset_lifecycle(&lifecycle);
		assert(mlc_domain_guard_acquire(&lifecycle, &guard) == -EUCLEAN);
		set_heap_state(&app_heap, self, 1, 1);
		reset_lifecycle(&lifecycle);
		assert(mlc_domain_guard_acquire(&lifecycle, &guard) == -EALREADY);
		set_heap_state(&app_heap, -1, 0, 1);
		reset_lifecycle(&lifecycle);
		g_critical_owned = true;
		assert(mlc_domain_guard_acquire(&lifecycle, &guard) == -EALREADY);
		g_critical_owned = false;
		reset_lifecycle(&lifecycle);
		assert(mlc_domain_guard_acquire(&lifecycle, &guard) == 0);
		assert(mlc_domain_guard_release(&lifecycle, &guard, 0) == 0);
		assert(g_irq_waitlock == 0);
	}

	assert(mm_loadable_domain_disable_and_wait(&descriptor) == 0);
	assert(mm_loadable_domain_finish_unload(&descriptor) == 0);
	printf("MLC_TASK6_GUARD status=PASS requested=%d completed=%d busy=%d preowned=%d accounting=%d remote=%d release=%d attempts=8 boundary=9999,10000 irqwaitlock=0 residue=0\n",
		repeat, repeat, repeat, repeat, repeat, repeat, repeat);
	return 0;
}
