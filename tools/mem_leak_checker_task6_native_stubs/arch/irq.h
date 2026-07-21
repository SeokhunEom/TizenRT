#ifndef __MLC_TASK6_NATIVE_ARCH_IRQ_H
#define __MLC_TASK6_NATIVE_ARCH_IRQ_H

#include <stdbool.h>
#include <tinyara/irq.h>

irqstate_t mlc_native_irqsave(void);
void mlc_native_irqrestore(irqstate_t flags);
bool mlc_native_interrupt_context(void);

static inline irqstate_t irqsave(void)
{
	return mlc_native_irqsave();
}

static inline void irqrestore(irqstate_t flags)
{
	mlc_native_irqrestore(flags);
}

static inline bool up_interrupt_context(void)
{
	return mlc_native_interrupt_context();
}

static inline bool up_irq_saved_enabled(irqstate_t flags)
{
	return flags == 0;
}

#endif
