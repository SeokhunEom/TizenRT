/* Host-only native-thread protocol stress. __atomic below MODELS individual
 * coherent word accesses/full fences for host sanitizers; production uses no
 * atomic API or exclusive instructions. This is not target timing evidence.
 */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define __KERNEL_HEALTH_MONITOR_ACCESS_H
static uint32_t health_load(const uint32_t *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static void health_store(uint32_t *p, uint32_t v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }
static void health_barrier(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static void test_panic(void);
#define PANIC() test_panic()
#include "../../os/kernel/health_monitor/health_monitor.c"

static __thread unsigned int cpu;
static __thread unsigned int masked;
static __thread bool in_irq;
static __thread struct tcb_s current;
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static unsigned int gate_count;
static unsigned int gate_phase;
static unsigned int operations;

/* Restrict CPU0 to one tick per batch. A host OS may suspend the producer
 * thread inside its fake IRQ-masked region; that is not an RTOS CPU failure.
 * Both threads still run concurrently throughout each batch.
 */
static void rendezvous(void)
{
	assert(!pthread_mutex_lock(&gate_lock));
	unsigned int phase = gate_phase;
	if (++gate_count == 2) {
		gate_count = 0;
		gate_phase++;
		assert(!pthread_cond_broadcast(&gate_cond));
	} else {
		while (phase == gate_phase) assert(!pthread_cond_wait(&gate_cond, &gate_lock));
	}
	assert(!pthread_mutex_unlock(&gate_lock));
}
irqstate_t irqsave(void) { unsigned int old = masked; masked = 1; return old; }
void irqrestore(irqstate_t old) { masked = old; }
bool up_interrupt_context(void) { return in_irq; }
int up_cpu_index(void) { return cpu; }
struct tcb_s *sched_self(void) { assert(masked); return &current; }
int up_putc(int c) { return c; }
void sched_foreach(void (*handler)(struct tcb_s *, void *), void *arg) { handler(&current, arg); }
static void test_panic(void)
{
	fprintf(stderr, "unexpected fault %u pid %d now %llu\n", g_fault, g_fault_pid, (unsigned long long)g_now);
	abort();
}
static void *producer(void *unused)
{
	(void)unused;
	cpu = 1;
	for (unsigned int round = 0; round < 5000; round++) {
		rendezvous();
		for (unsigned int i = 0; i < 64; i++) {
			current.pid = 1 + i % (CONFIG_MAX_TASKS - 1);
			unsigned int slot = PIDHASH(current.pid);
			int result;
			if (!health_load(&g_sources[slot].active)) result = health_monitor_start(UINT32_MAX);
			else if ((i + round) % 3) result = health_monitor_kick();
			else result = health_monitor_stop();
			assert(result == 0 || result == -ENOSPC || result == -EAGAIN);
			operations++;
		}
		rendezvous();
	}
	return NULL;
}
int main(void)
{
	pthread_t thread;
	assert(!pthread_create(&thread, NULL, producer, NULL));
	cpu = 0; masked = 1; in_irq = true;
	for (unsigned int round = 0; round < 5000; round++) {
		rendezvous();
		health_monitor_tick();
		rendezvous();
	}
	assert(!pthread_join(thread, NULL));
	health_monitor_tick();
	for (unsigned int slot = 0; slot < CONFIG_MAX_TASKS; slot++) {
		assert(g_cache[slot].active == (bool)health_load(&g_sources[slot].active));
	}
	assert(!g_fault && operations == 320000);
	puts("PASS: native-thread word-access model, 320000 API calls concurrent with 5000 CPU0 ticks");
	return 0;
}
