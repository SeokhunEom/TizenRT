/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_NUTTX_MM_ARCH_H
#define TIZENRT_NUTTX_MM_ARCH_H
#include <stdbool.h>
#ifdef TRMM_HOST_TEST
typedef unsigned long irqstate_t;
#else
#include <tinyara/irq.h>
#endif
bool trmm_interrupt_context(void);
irqstate_t trmm_irq_save(void);
void trmm_irq_restore(irqstate_t state);
#define up_interrupt_context trmm_interrupt_context
#define up_irq_save trmm_irq_save
#define up_irq_restore trmm_irq_restore
#endif
