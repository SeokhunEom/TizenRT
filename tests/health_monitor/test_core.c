/* Production-source tests: fake UP IRQ state, scheduler, UART and fatal trap. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void test_panic(void);
#define PANIC() test_panic()
#include "../../os/kernel/health_monitor/health_monitor.c"

static struct tcb_s current;
static unsigned int masked;
static bool in_irq;
static jmp_buf panic_env;
static char output[32768];
static size_t output_len;
static unsigned int panics;

irqstate_t irqsave(void) { unsigned int old = masked; masked = 1; return old; }
void irqrestore(irqstate_t old) { masked = old; }
bool up_interrupt_context(void) { return in_irq; }
struct tcb_s *sched_self(void) { assert(masked); return &current; }
int up_putc(int c) { assert(masked); assert(output_len + 1 < sizeof(output)); output[output_len++] = c; output[output_len] = 0; return c; }
void sched_foreach(void (*handler)(struct tcb_s *, void *), void *arg)
{
	assert(strstr(output, "HM fault=") == output);
	handler(&current, arg);
}
static void test_panic(void) { panics++; longjmp(panic_env, 1); }
static void tick(void) { masked = 1; in_irq = true; health_monitor_tick(); in_irq = false; masked = 0; }
static void reset(void)
{
	memset(g_sources, 0, sizeof(g_sources));
	memset(g_heap, 0, sizeof(g_heap));
	g_count = g_fault = 0; g_now = 0;
	g_fault_pid = 0; g_fault_deadline = 0;
	memset(&current, 0, sizeof(current)); current.pid = 1;
	masked = in_irq = output_len = panics = 0; output[0] = 0;
}
static void expect_fatal(unsigned int reason, pid_t pid)
{
	if (!setjmp(panic_env)) { tick(); assert(!"fatal must not return"); }
	assert(panics == 1 && g_fault == reason && g_fault_pid == pid);
	assert(strstr(output, "HM task pid="));
	in_irq = false; masked = 0;
	assert(health_monitor_kick() == -ESHUTDOWN);
	assert(health_monitor_stop() == -ESHUTDOWN);
	assert(health_monitor_start(10) == -ESHUTDOWN);
}
static void invariants(void)
{
	unsigned int i, count = 0;
	for (i = 0; i < CONFIG_MAX_TASKS; i++) {
		if (g_sources[i].active) count++;
	}
	assert(count == g_count);
	for (i = 0; i < g_count; i++) {
		struct health_source_s *s = &g_sources[g_heap[i].slot];
		assert(s->active && s->position == i);
		assert(g_heap[i].deadline <= s->deadline);
		if (i) assert(g_heap[(i - 1) / 2].deadline <= g_heap[i].deadline);
	}
}
int main(void)
{
	unsigned int i;
	reset();
	assert(health_monitor_start(0) == -EINVAL);
	assert(health_monitor_kick() == -ENOENT);
	assert(health_monitor_stop() == -ENOENT);
	in_irq = true;
	assert(health_monitor_start(1) == -EPERM);
	assert(health_monitor_kick() == -EPERM);
	assert(health_monitor_stop() == -EPERM);
	in_irq = false; current.pid = 0;
	assert(health_monitor_start(1) == -EINVAL);
	current.pid = 1;
	assert(health_monitor_start(11) == 0); /* Ceiling: two ticks */
	assert(health_monitor_start(100) == -EALREADY);
	tick(); assert(g_now == 1 && !g_fault);
	expect_fatal(1, 1);

	reset();
	assert(health_monitor_start(UINT32_MAX) == 0);
	assert(g_sources[1].timeout == 429496730ULL);
	assert(health_monitor_stop() == 0);
	/* Carry across the 32-bit tick boundary. */
	g_now = UINT32_MAX - 1ULL;
	assert(health_monitor_start(30) == 0);
	tick(); tick(); assert(g_now == 4294967296ULL);
	expect_fatal(1, 1);

	reset();
	assert(health_monitor_start(30) == 0);
	current.pid = 2; assert(health_monitor_start(40) == 0);
	tick(); tick(); current.pid = 1;
	assert(health_monitor_kick() == 0);
	assert(g_heap[0].deadline == 3); /* kick never reorders */
	tick(); assert(g_heap[0].slot == 2); invariants();
	expect_fatal(1, 2); /* stale earliest entry cannot hide another timeout */

	reset();
	/* Every node stale: same-tick repairs expose the true minimum. */
	for (i = 1; i < CONFIG_MAX_TASKS; i++) { current.pid = i; assert(!health_monitor_start(20)); }
	tick();
	for (i = 1; i < CONFIG_MAX_TASKS; i++) { current.pid = i; assert(!health_monitor_kick()); }
	tick(); invariants(); assert(g_heap[0].deadline == 3);
	expect_fatal(1, g_sources[g_heap[0].slot].pid);

	reset(); assert(!health_monitor_start(100));
	health_monitor_release(2); assert(!g_fault);
	health_monitor_release(1);
	health_monitor_release(1); assert(g_fault_pid == 1);
	current.pid = 17; /* PID hash reuse cannot overwrite pending evidence. */
	assert(health_monitor_start(100) == -ESHUTDOWN);
	expect_fatal(2, 1);

	reset(); assert(!health_monitor_start(10)); assert(!health_monitor_stop());
	health_monitor_release(1); tick(); assert(!g_fault && !g_count);
	current.pid = 17; assert(!health_monitor_start(20)); assert(!health_monitor_stop());

	reset(); assert(!health_monitor_start(10)); g_now = 1;
	assert(health_monitor_kick() == -ETIMEDOUT); expect_fatal(1, 1);
	reset(); assert(!health_monitor_start(10)); g_now = 1;
	assert(health_monitor_stop() == -ETIMEDOUT); expect_fatal(1, 1);

	/* Seeded membership churn checks arbitrary-position deletion and lazy
	 * lower-bound ordering, independent of the production heap algorithms. */
	reset(); srand(7);
	for (i = 0; i < 100000; i++) {
		unsigned int slot = 1 + rand() % (CONFIG_MAX_TASKS - 1);
		current.pid = slot;
		if (!g_sources[slot].active) assert(!health_monitor_start(100000 + rand() % 100000));
		else if (rand() & 1) assert(!health_monitor_stop());
		else assert(!health_monitor_kick());
		if (!(i % 200)) tick();
		invariants(); assert(!masked);
	}
	puts("PASS: API errors, deadline boundaries, 64-bit time, lazy roots, exit, PID reuse, fatal output, 100000 churn operations");
	return 0;
}
