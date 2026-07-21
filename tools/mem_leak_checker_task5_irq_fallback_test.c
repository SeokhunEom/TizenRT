#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <tinyara/irq.h>
#include <arch/irq.h>

irqstate_t g_fake_irq_state;
bool g_fake_interrupt_context;
void (*g_fake_irqsave_hook)(void);

int irq_try_enter_critical_fresh(irqstate_t *flags);

int main(void)
{
	irqstate_t flags = 0x55;
	unsigned int repeat;

	assert(irq_try_enter_critical_fresh(NULL) == -EINVAL);
	g_fake_interrupt_context = true;
	assert(irq_try_enter_critical_fresh(&flags) == -EPERM);
	assert(flags == 0x55);
	g_fake_interrupt_context = false;
	g_fake_irq_state = 1;
	assert(irq_try_enter_critical_fresh(&flags) == -EALREADY);
	assert(g_fake_irq_state == 1 && flags == 0x55);

	for (repeat = 0; repeat < 100; repeat++) {
		g_fake_irq_state = 0;
		flags = 0x55;
		assert(irq_try_enter_critical_fresh(&flags) == 0);
		assert(flags == 0 && g_fake_irq_state == 1);
		irqrestore(flags);
		assert(g_fake_irq_state == 0);
	}

	puts("MLC_TASK5_IRQ_ACTUAL variant=up_no_irqcount status=PASS repeat=100");
	return 0;
}
