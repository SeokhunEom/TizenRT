#ifndef __TINYARA_IRQ_H
#define __TINYARA_IRQ_H

typedef unsigned long irqstate_t;

int irq_try_enter_critical_fresh(irqstate_t *flags);
void leave_critical_section(irqstate_t flags);

#endif
