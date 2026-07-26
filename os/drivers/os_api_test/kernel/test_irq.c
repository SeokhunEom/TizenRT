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
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <debug.h>
#include <errno.h>

#include <tinyara/arch.h>
#include <tinyara/irq.h>
#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>

#if defined(CONFIG_SMP) && defined(CONFIG_IRQCOUNT)
#include "irq/irq.h"
#endif

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_IRQ_FIN_DATA1	0x13572468
#define TEST_IRQ_FIN_DATA2	0x24681357

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void test_irq_restore_fin_data(FAR struct tcb_s *tcb, int fin_data, int pending_fin_data)
{
	irqstate_t flags;

	flags = enter_critical_section();
	tcb->fin_data = fin_data;
	tcb->pending_fin_data = pending_fin_data;
	leave_critical_section(flags);
}

static int test_irq_critical_section(unsigned long arg)
{
	irqstate_t flags1;

	(void)arg;

#ifdef CONFIG_IRQCOUNT
	FAR struct tcb_s *self;
	irqstate_t flags2;
	int old_irqcount;

	self = sched_self();
	if (self == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	old_irqcount = self->irqcount;
	flags1 = enter_critical_section();
	if (self->irqcount != old_irqcount + 1) {
		dbg("enter_critical_section did not increment irqcount.\n");
		leave_critical_section(flags1);
		return ERROR;
	}

	flags2 = enter_critical_section();
	if (self->irqcount != old_irqcount + 2) {
		dbg("nested enter_critical_section did not increment irqcount.\n");
		leave_critical_section(flags2);
		leave_critical_section(flags1);
		return ERROR;
	}

	leave_critical_section(flags2);
	if (self->irqcount != old_irqcount + 1) {
		dbg("nested leave_critical_section did not decrement irqcount.\n");
		leave_critical_section(flags1);
		return ERROR;
	}

	leave_critical_section(flags1);
	if (self->irqcount != old_irqcount) {
		dbg("leave_critical_section did not restore irqcount.\n");
		return ERROR;
	}
#else
	flags1 = enter_critical_section();
	leave_critical_section(flags1);
#endif

	return OK;
}

static int test_irq_context_and_lock(unsigned long arg)
{
	irqstate_t flags;
#if defined(CONFIG_SMP) && defined(CONFIG_IRQCOUNT) && CONFIG_SMP_NCPUS > 1
	int cpu;
	int next_cpu;
#endif

	(void)arg;

	if (up_interrupt_context()) {
		dbg("test is unexpectedly running in interrupt context.\n");
		return ERROR;
	}

	flags = enter_critical_section();
	if (up_interrupt_context()) {
		dbg("critical section changed task context into interrupt context.\n");
		leave_critical_section(flags);
		return ERROR;
	}

#if defined(CONFIG_SMP) && defined(CONFIG_IRQCOUNT) && CONFIG_SMP_NCPUS > 1
	cpu = up_cpu_index();
	if (cpu < 0 || cpu >= CONFIG_SMP_NCPUS) {
		dbg("invalid cpu index: %d.\n", cpu);
		leave_critical_section(flags);
		return ERROR;
	}
	next_cpu = (cpu + 1) % CONFIG_SMP_NCPUS;

	if (irq_cpu_locked(cpu)) {
		dbg("irq_cpu_locked reports current cpu as externally locked: %d.\n", cpu);
		leave_critical_section(flags);
		return ERROR;
	}

	if (!irq_cpu_locked(next_cpu)) {
		dbg("irq_cpu_locked did not report lock for another cpu: %d/%d.\n", cpu, next_cpu);
		leave_critical_section(flags);
		return ERROR;
	}
#endif

	leave_critical_section(flags);
	if (up_interrupt_context()) {
		dbg("leaving critical section left interrupt context set.\n");
		return ERROR;
	}

	return OK;
}

static int test_irq_fin_notify_wait(unsigned long arg)
{
	FAR struct tcb_s *self;
	irqstate_t flags;
	int old_fin_data;
	int old_pending_fin_data;
	int ret;

	(void)arg;

	self = sched_self();
	if (self == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	flags = enter_critical_section();
	old_fin_data = self->fin_data;
	old_pending_fin_data = self->pending_fin_data;
	self->fin_data = NO_FIN_DATA;
	self->pending_fin_data = NO_FIN_DATA;
	leave_critical_section(flags);

	if (fin_notify(INVALID_PROCESS_ID, TEST_IRQ_FIN_DATA1) != ERROR) {
		dbg("fin_notify accepted invalid pid.\n");
		goto errout;
	}

	if (fin_notify(self->pid, TEST_IRQ_FIN_DATA1) != OK) {
		dbg("fin_notify failed for current task.\n");
		goto errout;
	}

	ret = fin_wait();
	if (ret != TEST_IRQ_FIN_DATA1) {
		dbg("fin_wait returned unexpected data: %d\n", ret);
		goto errout;
	}

	if (fin_notify(self->pid, TEST_IRQ_FIN_DATA1) != OK ||
		fin_notify(self->pid, TEST_IRQ_FIN_DATA2) != OK) {
		dbg("fin_notify failed while queueing pending data.\n");
		goto errout;
	}

	ret = fin_wait();
	if (ret != TEST_IRQ_FIN_DATA1) {
		dbg("fin_wait did not return first queued data: %d\n", ret);
		goto errout;
	}

	ret = fin_wait();
	if (ret != TEST_IRQ_FIN_DATA2) {
		dbg("fin_wait did not return pending queued data: %d\n", ret);
		goto errout;
	}

	test_irq_restore_fin_data(self, old_fin_data, old_pending_fin_data);
	return OK;

errout:
	test_irq_restore_fin_data(self, old_fin_data, old_pending_fin_data);
	return ERROR;
}

static int test_irq_all(unsigned long arg)
{
	if (test_irq_critical_section(arg) != OK) {
		return ERROR;
	}

	if (test_irq_context_and_lock(arg) != OK) {
		return ERROR;
	}

	if (test_irq_fin_notify_wait(arg) != OK) {
		return ERROR;
	}

	return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_irq(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_IRQ_TEST:
		ret = test_irq_all(arg);
		break;
	}

	return ret;
}
