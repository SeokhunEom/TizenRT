#include <tinyara/config.h>

#include <errno.h>

#include <tinyara/irq.h>
#include <arch/irq.h>

#if defined(CONFIG_SMP) && !defined(CONFIG_IRQCOUNT)
#error CONFIG_SMP requires CONFIG_IRQCOUNT
#endif

#ifndef CONFIG_IRQCOUNT
int irq_try_enter_critical_fresh(irqstate_t *flags)
{
#if defined(CONFIG_ARCH_CORTEXM3) || defined(CONFIG_ARCH_CORTEXM4) || \
	defined(CONFIG_ARCH_CORTEXM7) || defined(CONFIG_ARCH_ARMV7A_FAMILY)
	irqstate_t saved;

	if (flags == NULL) {
		return -EINVAL;
	}

	if (up_interrupt_context()) {
		return -EPERM;
	}

	saved = irqsave();
	if (!up_irq_saved_enabled(saved)) {
		irqrestore(saved);
		return -EALREADY;
	}

	*flags = saved;
	return 0;
#else
	(void)flags;
	return -ENOSYS;
#endif
}
#endif
