#ifndef __TEST_TINYARA_IRQ_H
#define __TEST_TINYARA_IRQ_H

#include <tinyara/sched.h>

typedef unsigned int irqstate_t;

static inline irqstate_t irqsave(void)
{
	return 0;
}

static inline void irqrestore(irqstate_t flags)
{
	(void)flags;
}

static inline int irq_try_enter_critical_fresh(irqstate_t *flags)
{
	*flags = 0;
	return 0;
}

static inline void leave_critical_section(irqstate_t flags)
{
	(void)flags;
}

static inline int this_cpu(void)
{
	return 0;
}

#endif
