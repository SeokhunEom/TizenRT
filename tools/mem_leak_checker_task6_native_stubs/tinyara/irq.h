#ifndef __MLC_TASK6_NATIVE_IRQ_H
#define __MLC_TASK6_NATIVE_IRQ_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t irqstate_t;

#ifdef CONFIG_IRQCOUNT
irqstate_t enter_critical_section(void);
void leave_critical_section(irqstate_t flags);
#else
#define enter_critical_section() irqsave()
#define leave_critical_section(flags) irqrestore(flags)
#endif

int irq_try_enter_critical_fresh(irqstate_t *flags);

#endif
