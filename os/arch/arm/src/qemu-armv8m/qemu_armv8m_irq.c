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
#include <debug.h>

#include <tinyara/irq.h>
#include <tinyara/arch.h>
#include <arch/irq.h>

#include "nvic.h"
#include "ram_vectors.h"
#include "up_arch.h"
#include "up_internal.h"

#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DEFPRIORITY32 \
	(NVIC_SYSH_PRIORITY_DEFAULT << 24 | \
	 NVIC_SYSH_PRIORITY_DEFAULT << 16 | \
	 NVIC_SYSH_PRIORITY_DEFAULT << 8  | \
	 NVIC_SYSH_PRIORITY_DEFAULT)

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern uint32_t _vectors[];

volatile uint32_t *current_regs;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int qemu_armv8m_irqinfo(int irq, uintptr_t *regaddr, uint32_t *bit,
			       bool enable)
{
	int extirq;

	if (irq >= MPS2_IRQ_FIRST && irq < NR_IRQS) {
		extirq = irq - MPS2_IRQ_FIRST;
		*regaddr = enable ? NVIC_IRQ_ENABLE(extirq) : NVIC_IRQ_CLEAR(extirq);
		*bit = 1 << (extirq & 31);
		return OK;
	}

	*regaddr = NVIC_SYSHCON;

	if (irq == MPS2_IRQ_MEMFAULT) {
		*bit = NVIC_SYSHCON_MEMFAULTENA;
	} else if (irq == MPS2_IRQ_BUSFAULT) {
		*bit = NVIC_SYSHCON_BUSFAULTENA;
	} else if (irq == MPS2_IRQ_USAGEFAULT) {
		*bit = NVIC_SYSHCON_USGFAULTENA;
	} else if (irq == MPS2_IRQ_SYSTICK) {
		*regaddr = NVIC_SYSTICK_CTRL;
		*bit = NVIC_SYSTICK_CTRL_ENABLE;
	} else {
		return ERROR;
	}

	return OK;
}

#ifdef CONFIG_ARMV8M_USEBASEPRI
static void qemu_armv8m_prioritize_syscall(int priority)
{
	uint32_t regval;

	regval = getreg32(NVIC_SYSH8_11_PRIORITY);
	regval &= ~NVIC_SYSH_PRIORITY_PR11_MASK;
	regval |= priority << NVIC_SYSH_PRIORITY_PR11_SHIFT;
	putreg32(regval, NVIC_SYSH8_11_PRIORITY);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_irqinitialize(void)
{
	uintptr_t regaddr;
	int nintlines;
	int i;

	nintlines = (getreg32(NVIC_ICTR) & NVIC_ICTR_INTLINESNUM_MASK) + 1;

	for (i = nintlines, regaddr = NVIC_IRQ0_31_ENABLE; i > 0; i--, regaddr += 4) {
		putreg32(0, regaddr);
	}

#ifdef CONFIG_ARCH_RAMVECTORS
	up_ramvec_initialize();
#endif

	putreg32((uint32_t)&_vectors, NVIC_VECTAB);

	putreg32(DEFPRIORITY32, NVIC_SYSH4_7_PRIORITY);
	putreg32(DEFPRIORITY32, NVIC_SYSH8_11_PRIORITY);
	putreg32(DEFPRIORITY32, NVIC_SYSH12_15_PRIORITY);

	for (i = (nintlines << 3), regaddr = NVIC_IRQ0_3_PRIORITY; i > 0; i--, regaddr += 4) {
		putreg32(DEFPRIORITY32, regaddr);
	}

	current_regs = NULL;

	irq_attach(MPS2_IRQ_SVCALL, up_svcall, NULL);
	irq_attach(MPS2_IRQ_HARDFAULT, up_hardfault, NULL);
	irq_attach(MPS2_IRQ_MEMFAULT, up_memfault, NULL);
	irq_attach(MPS2_IRQ_BUSFAULT, up_busfault, NULL);
	irq_attach(MPS2_IRQ_USAGEFAULT, up_usagefault, NULL);

#ifdef CONFIG_ARMV8M_USEBASEPRI
	qemu_armv8m_prioritize_syscall(NVIC_SYSH_SVCALL_PRIORITY);
#endif

#ifdef CONFIG_ARMV8M_MPU
	up_enable_irq(MPS2_IRQ_MEMFAULT);
#endif

#ifndef CONFIG_SUPPRESS_INTERRUPTS
	irqenable();
#endif
}

void up_disable_irq(int irq)
{
	uintptr_t regaddr;
	uint32_t regval;
	uint32_t bit;

	if (qemu_armv8m_irqinfo(irq, &regaddr, &bit, false) == OK) {
		if (irq >= MPS2_IRQ_FIRST) {
			putreg32(bit, regaddr);
		} else {
			regval = getreg32(regaddr);
			regval &= ~bit;
			putreg32(regval, regaddr);
		}
	}
}

void up_enable_irq(int irq)
{
	uintptr_t regaddr;
	uint32_t regval;
	uint32_t bit;

	if (qemu_armv8m_irqinfo(irq, &regaddr, &bit, true) == OK) {
		if (irq >= MPS2_IRQ_FIRST) {
			putreg32(bit, regaddr);
		} else {
			regval = getreg32(regaddr);
			regval |= bit;
			putreg32(regval, regaddr);
		}
	}
}

void up_ack_irq(int irq)
{
}

#ifdef CONFIG_ARCH_IRQPRIO
int up_prioritize_irq(int irq, int priority)
{
	uintptr_t regaddr;
	uint32_t regval;
	int shift;

	DEBUGASSERT(irq >= MPS2_IRQ_MEMFAULT && irq < NR_IRQS &&
		    (unsigned)priority <= NVIC_SYSH_PRIORITY_MIN);

	if (irq < MPS2_IRQ_FIRST) {
		regaddr = NVIC_SYSH_PRIORITY(irq);
		irq -= 4;
	} else {
		irq -= MPS2_IRQ_FIRST;
		regaddr = NVIC_IRQ_PRIORITY(irq);
	}

	regval = getreg32(regaddr);
	shift = (irq & 3) << 3;
	regval &= ~(0xff << shift);
	regval |= priority << shift;
	putreg32(regval, regaddr);

	return OK;
}
#endif
