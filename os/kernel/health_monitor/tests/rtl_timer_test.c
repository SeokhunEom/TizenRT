/* SPDX-License-Identifier: Apache-2.0 */
/* Real RTL timer disable/sample/enable/ISR, with only hardware registers modeled. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#define CONFIG_PM_TICKSUPPRESS 1
#define ARCH_TIMER_H
#define ARM_ARCH_TIMER_IRQ 27
#define GENERICTIMERFREQ 50000000
#define pdTICKS_TO_CNT 50000

typedef int (*xcpt_t)(int, uint32_t *);
static uint64_t g_counter;
static uint64_t g_compare;
static uint32_t g_aon;
static unsigned int g_ticks;
static bool g_enabled;

static uint64_t arm_arch_timer_count(void) { return g_counter; }
static uint64_t arm_arch_timer_compare(void) { return g_compare; }
static void arm_arch_timer_set_compare(uint64_t value) { g_compare = value; }
static void arm_arch_timer_enable(int enabled) { g_enabled = enabled; }
static void sched_process_timer(void) { g_ticks++; }
static void up_disable_irq(int irq) { (void)irq; }
static void up_enable_irq(int irq) { (void)irq; }
static void up_prioritize_irq(int irq, int priority) { (void)irq; (void)priority; }
static void irq_attach(int irq, xcpt_t handler, void *arg) { (void)irq; (void)handler; (void)arg; }
unsigned int up_cpu_index(void) { return 0; }
uint32_t SYSTIMER_TickGet(void) { return g_aon; }
clock_t up_timer_get_elapsedtick(void);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "../../../arch/arm/src/amebasmart/amebasmart_timerisr.c"
#pragma GCC diagnostic pop

static void reset(void)
{
	g_counter = 1000000;
	g_aon = 0;
	g_ticks = 0;
	up_timer_initialize();
	assert(g_enabled && g_compare == g_counter + pdTICKS_TO_CNT);
}

int main(void)
{
	reset();
	g_counter += 125 * pdTICKS_TO_CNT + pdTICKS_TO_CNT / 2;
	up_timer_disable();
	assert(!g_enabled && up_timer_get_elapsedtick() == 125);
	assert(up_timer_get_elapsedtick() == 125); /* Re-reading never consumes ticks. */
	g_ticks += up_timer_get_elapsedtick();
	up_timer_enable();
	assert(g_compare == g_counter + pdTICKS_TO_CNT / 2);
	assert(up_timerisr(0, NULL) == -1 && g_ticks == 125);
	g_counter = g_compare;
	assert(up_timerisr(0, NULL) == 0 && g_ticks == 126);

	reset();
	g_aon = UINT32_MAX - 100;
	g_counter += pdTICKS_TO_CNT / 4;
	up_timer_disable();
	g_aon += 4096; /* 125ms, including wrap of the always-on counter. */
	g_counter = 0; /* ARM timer resets during power gating. */
	assert(up_timer_get_elapsedtick() == 125);
	g_ticks += up_timer_get_elapsedtick();
	g_counter += 2 * pdTICKS_TO_CNT; /* Time after the compensation sample. */
	up_timer_enable();
	assert(g_compare == 3 * pdTICKS_TO_CNT / 4);
	assert(up_timerisr(0, NULL) == 0 && g_ticks == 127);
	assert(up_timerisr(0, NULL) == -1 && g_ticks == 127);

	reset();
	for (unsigned int attempt = 0; attempt < 100; attempt++) {
		g_counter += pdTICKS_TO_CNT / 4;
		up_timer_disable();
		g_ticks += up_timer_get_elapsedtick();
		up_timer_enable();
	}
	assert(g_ticks == 25); /* Short aborted attempts retain the ARM tick phase. */
	puts("PASS: RTL stopped timer pending ticks, abort, PG reset, AON wrap, phase and catchup");
	return 0;
}
