/* Included only in the ARMv8-M test configuration of os_api_test_drv.c. */
#include <tinyara/irq.h>
#include <tinyara/mm/mm.h>
#include <unistd.h>
#include <tinyara/wqueue.h>
#include "clock/clock.h"
#include <stdio.h>
#include "sched/sched.h"
#include "health_monitor/health_monitor.h"
#include "health_monitor/tests/armv8m/control.h"
#ifdef CONFIG_BINMGR_RECOVERY
#include "binary_manager/binary_manager_internal.h"
#endif

static int hm_status(void)
{
	struct entry { pid_t pid; uint32_t timeout, deadline; };
	struct entry *active = kmm_malloc(sizeof(*active) * CONFIG_MAX_TASKS);
	unsigned i, count = 0;
	uint32_t next = 0;
	int hint;
	irqstate_t flags;
	if (!active) return -ENOMEM;
	flags = irqsave();
	for (i = 0; i < CONFIG_MAX_TASKS; i++) {
		struct tcb_s *tcb = g_pidhash[i].tcb;
		if (tcb && tcb->health_monitor.timeout != 0) {
			active[count].pid = tcb->pid;
			active[count].timeout = tcb->health_monitor.timeout;
			active[count++].deadline = tcb->health_monitor.deadline;
		}
	}
	hint = health_monitor_next_check(&next);
	irqrestore(flags);
	for (i = 0; i < count; i++) {
		printf("HM_CONTROL TASK pid=%d timeout=%lu deadline=%lu\n", active[i].pid,
			(unsigned long)active[i].timeout, (unsigned long)active[i].deadline);
	}
	printf("HM_CONTROL STATUS count=%u hint=%d next=%lu\n", count, hint, (unsigned long)next);
	kmm_free(active);
	return 0;
}


#ifdef CONFIG_BINMGR_RECOVERY
static struct work_s g_hm_recover_work;

static void hm_recover_worker(FAR void *arg)
{
	/* LPWORK belongs to the kernel and survives removal of the caller app. */
	binary_manager_recovery(1);
}
#endif

static int hm_control_ioctl(int command)
{
	if (command == HM_TESTIOC_STATUS) return hm_status();
#ifdef CONFIG_BINMGR_RECOVERY
	if (command == HM_TESTIOC_RECOVER) {
		/* Return to user space before deactivating the requesting binary. */
		if (!work_available(&g_hm_recover_work)) return -EBUSY;
		return work_queue(LPWORK, &g_hm_recover_work, hm_recover_worker,
			NULL, MSEC2TICK(200));
	}
#endif
	return -EINVAL;
}
