/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the production implementation and its internal heap invariants
 * without adding a test-only interface to the product. Only OS primitives
 * and the TCB layout come from the host include directory.
 */

#include "../health_monitor.c"

#define TASK_COUNT (HEALTH_MONITOR_HEAP_CAPACITY + 1)

static _Thread_local struct tcb_s *g_test_current;
static _Thread_local unsigned int g_test_irq_masked;
static _Thread_local unsigned int g_test_lock_held;
static uint32_t g_test_now;
static struct tcb_s g_tasks[TASK_COUNT];

struct model_s {
	bool registered;
	uint32_t timeout;
	uint32_t deadline;
	uint32_t check_at;
};

static struct model_s g_model[TASK_COUNT];

irqstate_t irqsave(void)
{
	unsigned int previous = g_test_irq_masked;
	g_test_irq_masked = 1;
	return previous;
}

void irqrestore(irqstate_t flags)
{
	assert(g_test_lock_held == 0);
	g_test_irq_masked = flags;
}

void spin_lock_wo_note(volatile spinlock_t *lock)
{
	assert(g_test_irq_masked && !g_test_lock_held);
	while (__atomic_exchange_n(lock, SP_LOCKED, __ATOMIC_ACQUIRE) == SP_LOCKED) {
	}
	g_test_lock_held = 1;
}

void spin_unlock_wo_note(volatile spinlock_t *lock)
{
	assert(g_test_irq_masked && g_test_lock_held);
	g_test_lock_held = 0;
	__atomic_store_n(lock, SP_UNLOCKED, __ATOMIC_RELEASE);
}

struct tcb_s *this_task(void)
{
	assert(g_test_current != NULL);
	return g_test_current;
}

clock_t clock_systimer(void)
{
	assert(g_test_irq_masked);
	return __atomic_load_n(&g_test_now, __ATOMIC_RELAXED);
}

static void set_now(uint32_t now)
{
	__atomic_store_n(&g_test_now, now, __ATOMIC_RELAXED);
}

static void select_task(unsigned int index)
{
	assert(index < TASK_COUNT);
	g_test_current = &g_tasks[index];
}

static void reset_tasks(uint32_t now)
{
	unsigned int index;

	assert(g_health_count == 0);
	memset(g_tasks, 0xa5, sizeof(g_tasks));
	memset(g_model, 0, sizeof(g_model));
	set_now(now);
	for (index = 0; index < TASK_COUNT; index++) {
		g_tasks[index].pid = index + 2;
		health_monitor_task_init(&g_tasks[index]);
		assert(health_monitor_state(&g_tasks[index])->timeout == 0);
		assert(health_monitor_state(&g_tasks[index])->deadline == 0);
	}
	select_task(0);
}

/* The independent model compares signed distances with one current time.
 * Every check runs after the operation, or after concurrent workers join.
 */

static void verify_model(void)
{
	bool found[TASK_COUNT] = {false};
	uint32_t now = __atomic_load_n(&g_test_now, __ATOMIC_RELAXED);
	uint32_t next = 0xdeadbeef;
	uint32_t expected_next = 0;
	unsigned int count = 0;
	unsigned int index;
	int result;
	irqstate_t flags = health_monitor_lock();

	for (index = 0; index < TASK_COUNT; index++) {
		struct health_monitor_s *state = health_monitor_state(&g_tasks[index]);
		assert(state->timeout == g_model[index].timeout);
		assert(state->deadline == g_model[index].deadline);
		if (g_model[index].registered) {
			if (count == 0 || (int32_t)(g_model[index].check_at - now) <
				(int32_t)(expected_next - now)) {
				expected_next = g_model[index].check_at;
			}
			count++;
		}
	}

	assert(count == g_health_count);
	for (index = 0; index < g_health_count; index++) {
		unsigned int task = g_health_heap[index].tcb - g_tasks;
		assert(task < TASK_COUNT && !found[task]);
		found[task] = true;
		assert(g_model[task].registered);
		assert(g_health_heap[index].check_at == g_model[task].check_at);
		if (index > 0) {
			assert((int32_t)(g_health_heap[(index - 1) / 2].check_at - now) <=
				   (int32_t)(g_health_heap[index].check_at - now));
		}
	}

	result = health_monitor_next_check(&next);
	assert(result == (count != 0));
	assert(next == (count ? expected_next : 0xdeadbeef));
	health_monitor_unlock(flags);
	assert(!g_test_irq_masked && !g_test_lock_held);
}

static void start_task(unsigned int index, uint32_t timeout)
{
	uint32_t now = __atomic_load_n(&g_test_now, __ATOMIC_RELAXED);
	select_task(index);
	assert(health_monitor_start(timeout) == OK);
	g_model[index] = (struct model_s){true, timeout, now + timeout, now + timeout};
}

static void stop_task(unsigned int index)
{
	select_task(index);
	assert(health_monitor_stop() == OK);
	memset(&g_model[index], 0, sizeof(g_model[index]));
}

static void test_basic(void)
{
	struct health_monitor_entry_s heap_before[HEALTH_MONITOR_HEAP_CAPACITY];
	uint32_t sequence;
	irqstate_t flags;

	reset_tasks(100);
	assert(health_monitor_start(0) == -EINVAL);
	assert(health_monitor_start((uint32_t)INT32_MAX + 1) == -EINVAL);
	assert(health_monitor_start(UINT32_MAX) == -EINVAL);
	assert(health_monitor_stop() == -ENOENT);
	health_monitor_kick();
	health_monitor_cleanup(NULL);
	verify_model();

	start_task(0, 10);
	assert(health_monitor_start(20) == -EEXIST);
	memcpy(heap_before, g_health_heap, sizeof(heap_before));
	sequence = g_health_sequence;
	set_now(150); /* Late KICK is accepted before any inspector runs. */
	health_monitor_kick();
	g_model[0].deadline = 160;
	assert(memcmp(heap_before, g_health_heap, sizeof(heap_before)) == 0);
	assert(sequence == g_health_sequence);
	verify_model();

	flags = irqsave();
	health_monitor_kick();
	assert(g_test_irq_masked); /* Preserve a caller's already-masked IRQs. */
	irqrestore(flags);
	stop_task(0);
	health_monitor_cleanup(&g_tasks[0]);
	health_monitor_cleanup(&g_tasks[0]);
	verify_model();
}

static void test_heap_and_capacity(void)
{
	unsigned int index;
	unsigned int victim;

	reset_tasks(500);
	for (index = 0; index < HEALTH_MONITOR_HEAP_CAPACITY; index++) {
		start_task(index, (index * 73) % 997 + 1);
		verify_model();
	}
	select_task(HEALTH_MONITOR_HEAP_CAPACITY);
	assert(health_monitor_start(1) == -ENOSPC);
	verify_model();

	victim = g_health_heap[0].tcb - g_tasks;
	stop_task(victim);
	verify_model();
	victim = g_health_heap[g_health_count / 2].tcb - g_tasks;
	stop_task(victim);
	verify_model();
	victim = g_health_heap[g_health_count - 1].tcb - g_tasks;
	stop_task(victim);
	verify_model();
	start_task(HEALTH_MONITOR_HEAP_CAPACITY, 1);
	verify_model();

	while (g_health_count) {
		victim = g_health_heap[0].tcb - g_tasks;
		stop_task(victim);
		verify_model();
	}

	for (index = 0; index < 32; index++) {
		start_task(index, 10); /* Equal reservations are all retained. */
	}
	verify_model();
	for (index = 0; index < 32; index++) {
		stop_task(index);
		verify_model();
	}
}

static void test_time_boundaries(void)
{
	reset_tasks(UINT32_MAX);
	start_task(0, 1); /* Tick zero is a real reservation, not an empty heap. */
	start_task(1, 2);
	verify_model();
	set_now(1);
	verify_model();
	stop_task(0);
	stop_task(1);

	reset_tasks(100);
	start_task(0, 1);
	set_now(102);
	start_task(1, INT32_MAX); /* Raw key subtraction would reverse this pair. */
	verify_model();
	assert(g_health_heap[0].tcb == &g_tasks[0]);
	stop_task(0);
	verify_model();
	stop_task(1);
	verify_model();
}

static void test_snapshot_and_lifetime(void)
{
	struct tcb_s *victim;
	uint32_t next = 99;
	uint32_t sequence;
	irqstate_t flags;

	reset_tasks(300);
	flags = health_monitor_lock();
	sequence = __atomic_load_n(&g_health_sequence, __ATOMIC_RELAXED);
	__atomic_store_n(&g_health_sequence, sequence + 1, __ATOMIC_RELEASE);
	assert(health_monitor_next_check(&next) == -EAGAIN && next == 99);
	__atomic_store_n(&g_health_sequence, UINT32_MAX - 1, __ATOMIC_RELEASE);
	health_monitor_unlock(flags);
	start_task(0, 30); /* Even publication sequence wraps through zero. */
	verify_model();
	assert(g_health_sequence == 0);
	health_monitor_cleanup(&g_tasks[0]); /* Restart leaves the same TCB unregistered. */
	memset(&g_model[0], 0, sizeof(g_model[0]));
	health_monitor_kick();
	verify_model();
	start_task(0, 40);
	stop_task(0);

	victim = calloc(1, sizeof(*victim));
	assert(victim != NULL);
	victim->pid = g_tasks[0].pid;
	health_monitor_task_init(victim);
	g_test_current = victim;
	assert(health_monitor_start(10) == OK);
	select_task(1); /* Cleanup acts on the target, not the calling thread. */
	health_monitor_cleanup(victim);
	assert(health_monitor_state(victim)->timeout == 0);
	free(victim);
	verify_model();
	start_task(0, 20); /* Reuse the PID after old references are removed. */
	verify_model();
	stop_task(0);
}

static void test_randomized(void)
{
	uint32_t seed = 0x12345678;
	uint32_t now = UINT32_MAX - 1000;
	unsigned int iteration;
	unsigned int index;
	unsigned int operation;
	uint32_t timeout;

	reset_tasks(now);
	for (iteration = 0; iteration < 50000; iteration++) {
		seed = seed * 1664525U + 1013904223U;
		index = (seed >> 8) % 64;
		operation = (seed >> 16) % 4;
		now += (seed >> 24) % 7;
		set_now(now);
		select_task(index);
		if (operation == 0) {
			timeout = seed % 1000 + 1;
			if (g_model[index].registered) {
				assert(health_monitor_start(timeout) == -EEXIST);
			} else {
				start_task(index, timeout);
			}
		} else if (operation == 1) {
			health_monitor_kick();
			if (g_model[index].registered) {
				g_model[index].deadline = now + g_model[index].timeout;
			}
		} else if (operation == 2) {
			if (g_model[index].registered) {
				stop_task(index);
			} else {
				assert(health_monitor_stop() == -ENOENT);
			}
		} else {
			health_monitor_cleanup(&g_tasks[index]);
			memset(&g_model[index], 0, sizeof(g_model[index]));
		}
		verify_model();
	}
	for (index = 0; index < 64; index++) {
		health_monitor_cleanup(&g_tasks[index]);
		memset(&g_model[index], 0, sizeof(g_model[index]));
	}
	verify_model();
}

#ifdef CONFIG_SMP
static pthread_barrier_t g_worker_barrier;

static void wait_for_peer(void)
{
	int ret = pthread_barrier_wait(&g_worker_barrier);
	assert(ret == 0 || ret == PTHREAD_BARRIER_SERIAL_THREAD);
}

static void *registry_worker(void *arg)
{
	unsigned int base = (uintptr_t)arg;
	unsigned int iteration;
	unsigned int index;
	int ret;

	wait_for_peer();
	for (iteration = 0; iteration < 50000; iteration++) {
		index = base + iteration % 32;
		select_task(index);
		ret = health_monitor_start(index + 1);
		assert(ret == OK || ret == -EEXIST);
		health_monitor_kick();
		if (iteration & 1) {
			assert(health_monitor_stop() == OK);
		} else {
			health_monitor_cleanup(&g_tasks[index]);
		}
		assert(!g_test_irq_masked && !g_test_lock_held);
	}
	return NULL;
}

static void *snapshot_writer(void *arg)
{
	unsigned int iteration;
	(void)arg;
	wait_for_peer();
	for (iteration = 0; iteration < 100000; iteration++) {
		select_task(iteration & 1);
		assert(health_monitor_start((iteration & 1) ? 0x2222 : 0x1111) == OK);
		assert(health_monitor_stop() == OK);
	}
	return NULL;
}

static void *snapshot_reader(void *arg)
{
	unsigned int iteration;
	uint32_t next;
	int ret;
	(void)arg;
	wait_for_peer();
	for (iteration = 0; iteration < 1000000; iteration++) {
		next = 0xdeadbeef;
		ret = health_monitor_next_check(&next);
		assert(ret == 0 || ret == 1 || ret == -EAGAIN);
		if (ret == 1) {
			assert(next == 0x1111 || next == 0x2222);
		} else {
			assert(next == 0xdeadbeef);
		}
	}
	return NULL;
}

static void test_concurrent(void)
{
	pthread_t first;
	pthread_t second;
	reset_tasks(0);
	assert(pthread_barrier_init(&g_worker_barrier, NULL, 2) == 0);
	assert(pthread_create(&first, NULL, registry_worker, (void *)(uintptr_t)0) == 0);
	assert(pthread_create(&second, NULL, registry_worker, (void *)(uintptr_t)32) == 0);
	assert(pthread_join(first, NULL) == 0);
	assert(pthread_join(second, NULL) == 0);
	verify_model();
	assert(pthread_create(&first, NULL, snapshot_writer, NULL) == 0);
	assert(pthread_create(&second, NULL, snapshot_reader, NULL) == 0);
	assert(pthread_join(first, NULL) == 0);
	assert(pthread_join(second, NULL) == 0);
	verify_model();
	assert(pthread_barrier_destroy(&g_worker_barrier) == 0);
}
#endif

int main(void)
{
	test_basic();
	test_heap_and_capacity();
	test_time_boundaries();
	test_snapshot_and_lifetime();
	test_randomized();
#ifdef CONFIG_SMP
	test_concurrent();
	puts("PASS: SMP model, 50000 randomized operations, concurrent registry and snapshot stress");
#else
	puts("PASS: UP model, 50000 randomized operations");
#endif
	return 0;
}
