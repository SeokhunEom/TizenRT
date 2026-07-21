#ifndef __ARCH_IRQ_H
#define __ARCH_IRQ_H

#include <tinyara/irq.h>

extern irqstate_t g_fake_irq_state;
extern bool g_fake_interrupt_context;
extern void (*g_fake_irqsave_hook)(void);

static inline irqstate_t irqsave(void)
{
	irqstate_t saved = g_fake_irq_state;
	if (g_fake_irqsave_hook != NULL) {
		g_fake_irqsave_hook();
	}
	g_fake_irq_state = 1;
	return saved;
}

static inline void irqrestore(irqstate_t flags)
{
	g_fake_irq_state = flags;
}

static inline irqstate_t irqstate(void)
{
	return g_fake_irq_state;
}

static inline bool up_interrupt_context(void)
{
	return g_fake_interrupt_context;
}

static inline bool up_irq_saved_enabled(irqstate_t flags)
{
	return flags == 0;
}

#endif
