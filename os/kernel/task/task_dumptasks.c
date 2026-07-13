/****************************************************************************
 *
 * Copyright 2022 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#undef  CONFIG_DEBUG
#undef  CONFIG_DEBUG_ERROR
#define CONFIG_DEBUG 1
#define CONFIG_DEBUG_ERROR 1

#if defined(CONFIG_BINMGR_RECOVERY) && defined(CONFIG_BINARY_MANAGER)
#include <queue.h>
#endif
#include <debug.h>
#include <semaphore.h>
#include <sys/types.h>
#include <tinyara/arch.h>
#include <tinyara/sched.h>

#define SEM_DIAG_TRAVERSAL_LIMIT 64

/* NOTE : This file is for assert usage only. */

#ifdef CONFIG_STACK_COLORATION
#if CONFIG_TASK_NAME_SIZE > 0
#define TASKDUMP_ARGS_FORMAT "%*s | %5s | %4s | %7s / %7s | %16s | %8s | %10s \n"
#define TASKDUMP_FORMAT "%*s | %5d | %4d | %7lu / %7lu | %16p | %8p | %10u \n"
#define TASKDUMP_VALUE  CONFIG_TASK_NAME_SIZE, tcb->name, tcb->pid, tcb->sched_priority, (unsigned long)used_stack_size, \
	(unsigned long)tcb->adj_stack_size, tcb->stack_alloc_ptr, tcb, tcb->task_state
#define TASKDUMP_ARGS  CONFIG_TASK_NAME_SIZE, "NAME", "PID", "PRI", "USED", "TOTAL STACK",  "STACK ALLOC ADDR", "TCB ADDR", "TASK STATE"
#else
#define TASKDUMP_ARGS_FORMAT "%5s | %4s | %7s / %7s | %16s | %8s | %10s \n"
#define TASKDUMP_FORMAT "%5d | %4d | %7lu / %7lu | %16p | %8p | %10u \n"
#define TASKDUMP_VALUE  tcb->pid, tcb->sched_priority, tcb, (unsigned long)used_stack_size, (unsigned long)tcb->adj_stack_size\
	, tcb->stack_alloc_ptr, tcb, tcb->task_state
#define TASKDUMP_ARGS  "PID", "PRI", "USED", "TOTAL STACK", "STACK ALLOC ADDR", "TCB ADDR", "TASK STATE"
#endif
#else
#if CONFIG_TASK_NAME_SIZE > 0
#define TASKDUMP_ARGS_FORMAT "%*s | %5s | %4s | %7s | %16s | %8s | %10s \n"
#define TASKDUMP_FORMAT "%*s | %5d | %4d | %7lu | %16p | %8p | %10u \n"
#define TASKDUMP_VALUE  CONFIG_TASK_NAME_SIZE, tcb->name, tcb->pid, tcb->sched_priority, (unsigned long)tcb->adj_stack_size\
	, tcb->stack_alloc_ptr, tcb, tcb->task_state
#define TASKDUMP_ARGS  CONFIG_TASK_NAME_SIZE, "NAME", "PID", "PRI", "TOTAL STACK",  "STACK ALLOC ADDR", "TCB ADDR", "TASK STATE"
#else
#define TASKDUMP_ARGS_FORMAT "%5s | %4s | %7s | %16s | %8s | %10s \n"
#define TASKDUMP_FORMAT "%5d | %4d | %7lu | %16p | %8p | %10u \n"
#define TASKDUMP_VALUE  tcb->pid, tcb->sched_priority, tcb, (unsigned long)tcb->adj_stack_size, tcb->stack_alloc_ptr, tcb, tcb->task_state
#define TASKDUMP_ARGS  "PID", "PRI", "TOTAL STACK", "STACK ALLOC ADDR", "TCB ADDR", "TASK STATE"
#endif
#endif

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct sem_diag_waiters_s {
	FAR sem_t *sem;
	unsigned int count;
};

/****************************************************************************
 * Global Variables
 ****************************************************************************/

#if defined(CONFIG_BINMGR_RECOVERY) && defined(CONFIG_BINARY_MANAGER)
extern sq_queue_t g_sem_list;
#endif

/****************************************************************************
 * Private Variables
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void sem_diag_print_flags(uint8_t flags)
{
	uint8_t unknown = flags & ~(PRIOINHERIT_FLAGS_DISABLE | FLAGS_INITIALIZED | FLAGS_SIGSEM | FLAGS_SEM_MUTEX);

	lldbg("  flags      : raw=0x%02x prioinherit-disabled=%s initialized=%s sigsem=%s mutex=%s unknown=0x%02x\n",
		  flags,
		  (flags & PRIOINHERIT_FLAGS_DISABLE) != 0 ? "set" : "clear",
		  (flags & FLAGS_INITIALIZED) != 0 ? "set" : "clear",
		  (flags & FLAGS_SIGSEM) != 0 ? "set" : "clear",
		  (flags & FLAGS_SEM_MUTEX) != 0 ? "set" : "clear",
		  unknown);
}

static void sem_diag_print_summary(FAR const char *label, FAR sem_t *sem)
{
	if (sem == NULL) {
		lldbg("%s: unavailable (sem=NULL)\n", label);
		return;
	}

	lldbg("%s: sem=%p semcount=%d flags=0x%02x\n", label, sem, sem->semcount, sem->flags);
	sem_diag_print_flags(sem->flags);
}

static void sem_diag_waiter_dump(FAR struct tcb_s *tcb, FAR void *arg)
{
	FAR struct sem_diag_waiters_s *ctx = (FAR struct sem_diag_waiters_s *)arg;

	if (tcb->waitsem == ctx->sem) {
#if CONFIG_TASK_NAME_SIZE > 0
		lldbg("  waiter[%u]: tcb=%p pid=%d state=%u name=%s\n", ctx->count, tcb, tcb->pid, tcb->task_state, tcb->name);
#else
		lldbg("  waiter[%u]: tcb=%p pid=%d state=%u\n", ctx->count, tcb, tcb->pid, tcb->task_state);
#endif
		ctx->count++;
	}
}

static void sem_diag_print_waiters(FAR sem_t *sem)
{
	struct sem_diag_waiters_s ctx;

	if (sem == NULL) {
		lldbg_noarg("  waiters    : unavailable (sem=NULL)\n");
		return;
	}

	ctx.sem = sem;
	ctx.count = 0;

	sched_foreach(sem_diag_waiter_dump, &ctx);

	if (ctx.count == 0) {
		lldbg_noarg("  waiters    : none\n");
	}
}

#ifdef SAVE_SEM_HOLDER
static void sem_diag_print_holder(FAR struct semholder_s *holder, unsigned int index)
{
	if (holder == NULL) {
		lldbg("  holder[%u]: none\n", index);
		return;
	}

	if (holder->htcb == NULL) {
		lldbg("  holder[%u]: invalid/corrupt (htcb=NULL)\n", index);
		return;
	}

#if CONFIG_TASK_NAME_SIZE > 0
	lldbg("  holder[%u]: tcb=%p pid=%d counts=%d name=%s\n", index, holder->htcb, holder->htcb->pid, holder->counts, holder->htcb->name);
#else
	lldbg("  holder[%u]: tcb=%p pid=%d counts=%d\n", index, holder->htcb, holder->htcb->pid, holder->counts);
#endif
}

static void sem_diag_print_holders(FAR sem_t *sem)
{
	unsigned int printed = 0;
#if CONFIG_SEM_PREALLOCHOLDERS > 0
	FAR struct semholder_s *holder;
#endif

	if (sem == NULL) {
		lldbg_noarg("  holders    : unavailable (sem=NULL)\n");
		return;
	}

#if CONFIG_SEM_PREALLOCHOLDERS > 0
	for (holder = sem->hhead;
		 holder != NULL && printed < SEM_DIAG_TRAVERSAL_LIMIT;
		 holder = holder->flink) {
		sem_diag_print_holder(holder, printed);
		printed++;
	}

	if (holder != NULL) {
		lldbg_noarg("  holders    : truncated (possible corrupt list)\n");
	}
#else
	if (sem->holder.htcb != NULL || sem->holder.counts != 0) {
		sem_diag_print_holder(&sem->holder, printed);
		printed++;
	}
#endif

	if (printed == 0) {
		lldbg_noarg("  holders    : none\n");
	}
}
#endif

#ifdef CONFIG_PRIORITY_INHERITANCE
static void sem_diag_print_tcb_holdsem(FAR struct tcb_s *tcb)
{
	FAR struct semholder_s *holder;
	unsigned int printed = 0;

	if (tcb == NULL) {
		lldbg_noarg("  held sems  : unavailable (tcb=NULL)\n");
		return;
	}

	for (holder = tcb->holdsem;
		 holder != NULL && printed < SEM_DIAG_TRAVERSAL_LIMIT;
		 holder = holder->tlink) {
		lldbg("  held[%u]   : sem=%p counts=%d\n", printed, holder->sem, holder->counts);
		if (holder->sem != NULL) {
			sem_diag_print_summary("    summary", holder->sem);
		}
		printed++;
	}

	if (printed == 0) {
		lldbg_noarg("  held sems  : none\n");
	} else if (holder != NULL) {
		lldbg_noarg("  held sems  : truncated (possible corrupt list)\n");
	}
}
#endif

static void sem_diag_print_tcb(FAR struct tcb_s *tcb)
{
	lldbg_noarg("===========================================================\n");
	lldbg_noarg("Semaphore diagnostics\n");
	lldbg_noarg("===========================================================\n");

	if (tcb == NULL) {
		lldbg_noarg("  asserted   : unavailable (tcb=NULL)\n");
		return;
	}

#if CONFIG_TASK_NAME_SIZE > 0
	lldbg("  asserted   : tcb=%p pid=%d name=%s\n", tcb, tcb->pid, tcb->name);
#else
	lldbg("  asserted   : tcb=%p pid=%d\n", tcb, tcb->pid);
#endif
	lldbg("  wait state : %u\n", tcb->task_state);
	sem_diag_print_summary("  waited sem", tcb->waitsem);
	sem_diag_print_waiters(tcb->waitsem);
#ifdef SAVE_SEM_HOLDER
	sem_diag_print_holders(tcb->waitsem);
#else
	lldbg_noarg("  holders    : unavailable (SAVE_SEM_HOLDER disabled)\n");
#endif
#ifdef CONFIG_PRIORITY_INHERITANCE
	sem_diag_print_tcb_holdsem(tcb);
#else
	lldbg_noarg("  held sems  : unavailable (CONFIG_PRIORITY_INHERITANCE disabled)\n");
#endif
}

static void sem_diag_print_tcb_all(FAR struct tcb_s *tcb, FAR void *arg)
{
	(void)arg;

#if CONFIG_TASK_NAME_SIZE > 0
	lldbg("  tcb        : tcb=%p pid=%d name=%s waitsem=%p\n", tcb, tcb->pid, tcb->name, tcb->waitsem);
#else
	lldbg("  tcb        : tcb=%p pid=%d waitsem=%p\n", tcb, tcb->pid, tcb->waitsem);
#endif
	if (tcb->waitsem != NULL) {
		sem_diag_print_summary("  waited sem", tcb->waitsem);
#ifdef SAVE_SEM_HOLDER
		sem_diag_print_holders(tcb->waitsem);
#else
		lldbg_noarg("  holders    : unavailable (SAVE_SEM_HOLDER disabled)\n");
#endif
	}
#ifdef CONFIG_PRIORITY_INHERITANCE
	sem_diag_print_tcb_holdsem(tcb);
#else
	lldbg_noarg("  held sems  : unavailable (CONFIG_PRIORITY_INHERITANCE disabled)\n");
#endif
}

#if defined(CONFIG_BINMGR_RECOVERY) && defined(CONFIG_BINARY_MANAGER)
static void sem_diag_print_global_list(void)
{
	FAR sem_t *sem;
	unsigned int printed = 0;

	lldbg_noarg("===========================================================\n");
	lldbg_noarg("Recovery-managed kernel semaphore registry\n");
	lldbg_noarg("===========================================================\n");

	sem = (FAR sem_t *)sq_peek(&g_sem_list);
	if (sem == NULL) {
		lldbg_noarg("  recovery sem registry: empty\n");
		return;
	}

	while (sem != NULL && printed < SEM_DIAG_TRAVERSAL_LIMIT) {
		lldbg("  registry[%u]\n", printed);
		sem_diag_print_summary("    summary", sem);
		printed++;
		sem = sq_next(sem);
	}

	if (sem != NULL) {
		lldbg_noarg("  recovery sem registry: truncated (possible corrupt list)\n");
	}

	lldbg("  printed registry count: %u\n", printed);
}
#endif

/****************************************************************************
 * Name: task_show_tcbinfo
 * 
 * NOTE : This function is for assert usage only.
 ****************************************************************************/

void task_show_tcbinfo(struct tcb_s *tcb)
{
#ifdef CONFIG_LIB_SYSCALL
	int nsyscall = tcb->xcp.nsyscalls;
#endif
	lldbg_noarg("===========================================================\n");
	lldbg_noarg("Asserted task's TCB info \n");
	lldbg_noarg("===========================================================\n");
	lldbg("State       : %u\n", tcb->task_state);
	lldbg("Flags       : %u\n", tcb->flags);
	lldbg("Lock count  : %u\n", tcb->lockcount);
#if CONFIG_RR_INTERVAL > 0
	lldbg("Timeslice   : %d\n", tcb->timeslice);
#endif
	lldbg("Waitdog     : %p\n", tcb->waitdog);
	lldbg("WaitSem     : %p\n", tcb->waitsem);
#ifndef CONFIG_DISABLE_MQUEUE
	lldbg("MsgwaitQ    : %p\n", tcb->msgwaitq);
#endif
#ifndef CONFIG_DISABLE_SIGNALS
	lldbg("Sigdeliver  : %p\n", tcb->xcp.sigdeliver);
#endif
#ifdef CONFIG_LIB_SYSCALL
	lldbg("Nsyscalls   : %u\n", nsyscall);
	for (int i = 0; i < nsyscall; i++) {
		lldbg("Syscall %d   : %p\n", i, tcb->xcp.syscall[i].sysreturn);
	}
#endif
	sem_diag_print_tcb(tcb);
}

/****************************************************************************
 * Name: task_taskdump
 * 
 * NOTE : This function is for assert usage only.
 ****************************************************************************/

static void task_taskdump(FAR struct tcb_s *tcb, FAR void *arg)
{
#ifdef CONFIG_STACK_COLORATION
	size_t used_stack_size = up_check_tcbstack(tcb);
#endif

	/* Dump interesting properties of this task */

	lldbg_noarg(TASKDUMP_FORMAT, TASKDUMP_VALUE);

#ifdef CONFIG_STACK_COLORATION
	if (used_stack_size >= tcb->adj_stack_size) {
		lldbg_noarg("  !!! PID (%d) STACK OVERFLOW !!! \n", tcb->pid);
	}
#endif
}

/****************************************************************************
 * Name: task_show_alivetask_list
 * 
 * NOTE : This function is for assert usage only.
 ****************************************************************************/

void task_show_alivetask_list(void)
{
	lldbg_noarg("===========================================================\n");
	lldbg_noarg("List of all tasks in the system\n");
	lldbg_noarg("===========================================================\n");

	lldbg_noarg(TASKDUMP_ARGS_FORMAT, TASKDUMP_ARGS);
	lldbg_noarg("-------------------------------------------------------------------------------------------------------------------\n");

	/* Dump interesting properties of each task in the crash environment */

	sched_foreach(task_taskdump, NULL);
	lldbg_noarg("-------------------------------------------------------------------------------------------------------------------\n");

	lldbg_noarg("===========================================================\n");
	lldbg_noarg("All-TCB semaphore diagnostics\n");
	lldbg_noarg("===========================================================\n");
	sched_foreach(sem_diag_print_tcb_all, NULL);

#if defined(CONFIG_BINMGR_RECOVERY) && defined(CONFIG_BINARY_MANAGER)
	sem_diag_print_global_list();
#endif

}
