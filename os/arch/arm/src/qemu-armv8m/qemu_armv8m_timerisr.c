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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <stdint.h>
#include <time.h>

#include <tinyara/irq.h>
#include <arch/irq.h>

#include "clock/clock.h"
#include "nvic.h"
#include "up_arch.h"
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TIMER_RELOAD (MPS2_AN505_SYSCLK_FREQUENCY / CLK_TCK)

#if TIMER_RELOAD == 0 || (MPS2_AN505_SYSCLK_FREQUENCY % CLK_TCK) != 0
#error System clock must provide an integral, nonzero number of counts per tick
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Last accounted counter value; retaining the remainder prevents drift. */
static uint32_t g_timer_last;

static int qemu_armv8m_timerisr(int irq, FAR void *context, FAR void *arg)
{
	uint32_t elapsed;
	uint32_t ticks;

	putreg32(MPS2_TIMER_INTSTATUS_IRQ, MPS2_TIMER0_INTSTATUS);

	/* TIMER0 has a single pending bit, so delayed interrupts can coalesce.
	 * Account elapsed TIMER1 counts instead of assuming one tick per IRQ.
	 * Unsigned subtraction handles the 32-bit counter wrap. TIMER1 must be
	 * sampled at least once per full counter period (about 214 seconds).
	 */

	elapsed = g_timer_last - getreg32(MPS2_TIMER1_VALUE);
	ticks = elapsed / TIMER_RELOAD;
	g_timer_last -= ticks * TIMER_RELOAD;
	while (ticks-- > 0) {
		sched_process_timer();
	}
	return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_timer_initialize(void)
{
#ifdef CONFIG_ARCH_IRQPRIO
	up_prioritize_irq(MPS2_IRQ_TIMER0, NVIC_SYSH_PRIORITY_DEFAULT);
#else
	uint32_t regval;

	regval = getreg32(NVIC_SYSH12_15_PRIORITY);
	regval &= ~NVIC_SYSH_PRIORITY_PR15_MASK;
	regval |= NVIC_SYSH_PRIORITY_MIN << NVIC_SYSH_PRIORITY_PR15_SHIFT;
	putreg32(regval, NVIC_SYSH12_15_PRIORITY);
#endif

	putreg32(0, MPS2_TIMER0_CTRL);
	putreg32(MPS2_TIMER_INTSTATUS_IRQ, MPS2_TIMER0_INTSTATUS);
	putreg32(TIMER_RELOAD, MPS2_TIMER0_RELOAD);

	(void)irq_attach(MPS2_IRQ_TIMER0, qemu_armv8m_timerisr, NULL);

	putreg32(0, MPS2_TIMER1_CTRL);
	putreg32(MPS2_TIMER_INTSTATUS_IRQ, MPS2_TIMER1_INTSTATUS);
	putreg32(UINT32_MAX, MPS2_TIMER1_RELOAD);
	putreg32(MPS2_TIMER_CTRL_EN, MPS2_TIMER1_CTRL);
	g_timer_last = getreg32(MPS2_TIMER1_VALUE);

	up_enable_irq(MPS2_IRQ_TIMER0);

	putreg32(MPS2_TIMER_CTRL_EN | MPS2_TIMER_CTRL_IRQEN, MPS2_TIMER0_CTRL);
	__asm__ __volatile__("dsb");
	__asm__ __volatile__("isb");
}
