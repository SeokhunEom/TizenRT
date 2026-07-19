/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
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
 * kernel/task/task_setup.c
 *
 *   Copyright (C) 2007-2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <sched.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <tinyara/arch.h>
#include <tinyara/irq.h>
#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_PROCESS)
#include <tinyara/kmalloc.h>
#endif
#ifdef CONFIG_SCHED_CPULOAD
#include <tinyara/clock.h>
#endif

#include "sched/sched.h"
#include "pthread/pthread.h"
#include "group/group.h"
#include "task/task.h"
#include "clock/clock.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
/* This is an artificial limit to detect error conditions where an argv[]
 * list is not properly terminated.
 */

#define MAX_STACK_ARGS 256

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

/****************************************************************************
 * Global Variables
 ****************************************************************************/

/****************************************************************************
 * Private Variables
 ****************************************************************************/

/* This is the name for un-named tasks */

static const char g_noname[] = "<noname>";

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int task_assignpid(FAR struct tcb_s *tcb);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: task_assignpid
 *
 * Description:
 *   This function assigns the next unique task ID to a task.
 *
 * Inputs:
 *   tcb - TCB of task
 *
 * Return:
 *   OK on success; ERROR on failure.
 *   errno is set as EBUSY, if already CONFIG_MAX_TASKS are running in system
 *
 ****************************************************************************/

static int task_assignpid(FAR struct tcb_s *tcb)
{
	pid_t next_pid;
	int hash_ndx;
	int tries;

	/* Disable pre-emption.  This should provide sufficient protection
	 * for the following operation.
	 */

	irqstate_t flags = enter_critical_section();

	/* We'll try every allowable pid */

	for (tries = 0; tries < CONFIG_MAX_TASKS; tries++) {
		/* Get the next process ID candidate */

		next_pid = ++g_lastpid;

		/* Verify that the next_pid is in the valid range */
#ifdef CONFIG_DEBUG_MM_HEAPINFO
		/* Last two values(INT16_MAX and INT16_MAX - 1) are used for heapinfo. So pid can be smaller than (INT16_MAX - 1). */
		if (next_pid == (INT16_MAX - 1)) {
			g_lastpid = 1;
			next_pid = 1;
		}
#else
		if (next_pid <= 0) {
			g_lastpid = 1;
			next_pid = 1;
		}
#endif

		/* Get the hash_ndx associated with the next_pid */

		hash_ndx = PIDHASH(next_pid);

		/* Check if there is a (potential) duplicate of this pid */
		if (!g_pidhash[hash_ndx].tcb) {
			/* Assign this PID to the task */
			g_pidhash[hash_ndx].tcb = tcb;
			g_pidhash[hash_ndx].pid = next_pid;
#ifdef CONFIG_SCHED_CPULOAD
			int cpuload_idx;
			for (cpuload_idx = 0; cpuload_idx < SCHED_NCPULOAD; cpuload_idx++) {
				for (int cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++) {
					g_pidhash[hash_ndx].ticks[cpu][cpuload_idx] = 0;
				}
			}
#endif
			tcb->pid = next_pid;

			/* Increment the task count */
			g_alive_taskcount++;

			leave_critical_section(flags);
			return OK;
		}
	}

	/* If we get here, then the g_pidhash[] table is completely full.
	 * We cannot allow another task to be started.
	 */

	leave_critical_section(flags);
	set_errno(EBUSY);
	return ERROR;
}

/****************************************************************************
 * Name: task_inherit_affinity
 *
 * Description:
 *   exec(), task_create(), and vfork() all inherit the affinity mask from
 *   the parent thread.  This is the default for pthread_create() as well
 *   but the affinity mask can be specified in the pthread attributes as
 *   well.  pthread_setup() will have to fix up the affinity mask in this
 *   case.
 *
 * Input Parameters:
 *   tcb - The TCB of the new task.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The parent of the new task is the task at the head of the assigned task
 *   list for the current CPU.
 *
 ****************************************************************************/

#ifdef CONFIG_SMP
static inline void task_inherit_affinity(FAR struct tcb_s *tcb)
{
	FAR struct tcb_s *rtcb = this_task();
	tcb->affinity = rtcb->affinity;
}
#else
#define task_inherit_affinity(tcb)
#endif

/****************************************************************************
 * Name: task_saveparent
 *
 * Description:
 *   Save the task ID of the parent task in the child task's TCB and allocate
 *   a child status structure to catch the child task's exit status.
 *
 * Parameters:
 *   tcb   - The TCB of the new, child task.
 *   ttype - Type of the new thread: task, pthread, or kernel thread
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The parent of the new task is the task at the head of the ready-to-run
 *   list.
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_HAVE_PARENT
static inline void task_saveparent(FAR struct tcb_s *tcb, uint8_t ttype)
{
	FAR struct tcb_s *rtcb = this_task();

#if defined(HAVE_GROUP_MEMBERS) || defined(CONFIG_SCHED_CHILD_STATUS)
	DEBUGASSERT(tcb != NULL && tcb->group != NULL && rtcb->group != NULL);
#else
#endif

#ifdef HAVE_GROUP_MEMBERS
	/* Save the ID of the parent tasks' task group in the child's task group.
	 * Do nothing for pthreads.  The parent and the child are both members of
	 * the same task group.
	 */

#ifndef CONFIG_DISABLE_PTHREAD
	if ((tcb->flags & TCB_FLAG_TTYPE_MASK) != TCB_FLAG_TTYPE_PTHREAD)
#endif
	{
		/* This is a new task in a new task group, we have to copy the ID from
		 * the parent's task group structure to child's task group.
		 */

		tcb->group->tg_pgid = rtcb->group->tg_gid;
	}

#else
	DEBUGASSERT(tcb);

#ifndef CONFIG_DISABLE_PTHREAD
	if ((tcb->flags & TCB_FLAG_TTYPE_MASK) != TCB_FLAG_TTYPE_PTHREAD)
#endif
	{
		/* Save the parent task's ID in the child task's group. */

		tcb->group->tg_ppid = rtcb->pid;
	}
#endif

#ifndef CONFIG_DISABLE_PTHREAD
	if ((tcb->flags & TCB_FLAG_TTYPE_MASK) != TCB_FLAG_TTYPE_PTHREAD)
#endif
	{
#ifdef CONFIG_SCHED_CHILD_STATUS
		/* Tasks can also suppress retention of their child status by applying
		 * the SA_NOCLDWAIT flag with sigaction().
		 */

		if ((rtcb->group->tg_flags && GROUP_FLAG_NOCLDWAIT) == 0) {
			FAR struct child_status_s *child;

			/* Make sure that there is not already a structure for this PID in the
			 * parent TCB.  There should not be.
			 */

			child = group_findchild(rtcb->group, tcb->pid);
			DEBUGASSERT(!child);
			if (!child) {
				/* Allocate a new status structure  */

				child = group_allocchild();
			}

			/* Did we successfully find/allocate the child status structure? */

			DEBUGASSERT(child);
			if (child) {
				/* Yes.. Initialize the structure */

				child->ch_flags = ttype;
				child->ch_pid = tcb->pid;
				child->ch_status = 0;

				/* Add the entry into the TCB list of children */

				group_addchild(rtcb->group, child);
			}
		}
#else
		DEBUGASSERT(rtcb->group != NULL && rtcb->group->tg_nchildren < UINT16_MAX);
		rtcb->group->tg_nchildren++;
#endif
	}
}
#else
#define task_saveparent(tcb, ttype)
#endif

/****************************************************************************
 * Name: task_abortparent
 *
 * Description:
 *   Reverse the parent bookkeeping performed by task_saveparent() for a task
 *   that will not be activated.
 *
 * Assumptions:
 *   The caller holds a critical section and the TCB has not been activated.
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_HAVE_PARENT
static inline void task_abortparent(FAR struct tcb_s *tcb)
{
	FAR struct task_group_s *pgrp = NULL;
#ifndef HAVE_GROUP_MEMBERS
	FAR struct tcb_s *ptcb;
#endif

	DEBUGASSERT(tcb != NULL && tcb->group != NULL);

#ifndef CONFIG_DISABLE_PTHREAD
	if ((tcb->flags & TCB_FLAG_TTYPE_MASK) == TCB_FLAG_TTYPE_PTHREAD) {
		return;
	}
#endif

#ifdef HAVE_GROUP_MEMBERS
	pgrp = group_findbygid(tcb->group->tg_pgid);
#else
	ptcb = sched_gettcb(tcb->group->tg_ppid);
	if (ptcb != NULL) {
		pgrp = ptcb->group;
	}
#endif

	/* The parent's group can already be gone.  Its group teardown owns any
	 * child status entry that was still attached to it.
	 */

	if (pgrp != NULL) {
#ifdef CONFIG_SCHED_CHILD_STATUS
		FAR struct child_status_s *child;

		child = group_removechild(pgrp, tcb->pid);
		if (child != NULL) {
			group_freechild(child);
		}
#else
		DEBUGASSERT(pgrp->tg_nchildren > 0);
		if (pgrp->tg_nchildren > 0) {
			pgrp->tg_nchildren--;
		}
#endif
	}
}
#else
#define task_abortparent(tcb)
#endif

/****************************************************************************
 * Name: task_dupdspace
 *
 * Description:
 *   When a new task or thread is created from a PIC module, then that
 *   module (probably) intends the task or thread to execute in the same
 *   D-Space.  This function will duplicate the D-Space for that purpose.
 *
 * Parameters:
 *   tcb - The TCB of the new task.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The parent of the new task is the task at the head of the ready-to-run
 *   list.
 *
 ****************************************************************************/

#ifdef CONFIG_PIC
static inline void task_dupdspace(FAR struct tcb_s *tcb)
{
	FAR struct tcb_s *rtcb = this_task();
	if (rtcb->dspace != NULL) {
		/* Copy the D-Space structure reference and increment the reference
		 * count on the memory.  The D-Space memory will persist until the
		 * last thread exits (see sched_releasetcb()).
		 */

		tcb->dspace = rtcb->dspace;
		tcb->dspace->crefs++;
	}
}
#else
#define task_dupdspace(tcb)
#endif

/****************************************************************************
 * Name: thread_schedsetup
 *
 * Description:
 *   This functions initializes the common portions of the Task Control Block
 *   (TCB) in preparation for starting a new thread.
 *
 *   thread_schedsetup() is called from task_schedsetup() and
 *   pthread_schedsetup().
 *
 * Input Parameters:
 *   tcb        - Address of the new task's TCB
 *   priority   - Priority of the new task
 *   start      - Thread startup routine
 *   entry      - Thread user entry point
 *   ttype      - Type of the new thread: task, pthread, or kernel thread
 *
 * Return Value:
 *   OK on success; ERROR on failure.
 *
 *   This function can fail for two reasons.
 *   1) If requested priority is beyond the allowed range, errno = EINVAL
 *   2) If it is unable to assign a new, unique task ID to the TCB. errno = EBUSY
 *   errno is set accodingly.
 *
 ****************************************************************************/

static int thread_schedsetup(FAR struct tcb_s *tcb, int priority, start_t start, CODE void *entry, uint8_t ttype)
{
	int ret;
#ifdef CONFIG_APP_BINARY_SEPARATION
	struct tcb_s *rtcb;
#endif

	if (priority < SCHED_PRIORITY_MIN || priority > SCHED_PRIORITY_MAX) {
		set_errno(EINVAL);
		return ERROR;
	}

	/* Assign a unique task ID to the task. */

	ret = task_assignpid(tcb);
	if (ret == OK) {
		/* Save task priority and entry point in the TCB */

		tcb->sched_priority = (uint8_t)priority;
#ifdef CONFIG_PRIORITY_INHERITANCE
		tcb->base_priority = (uint8_t)priority;
#endif
		tcb->start = start;
		tcb->entry.main = (main_t)entry;

		/* Save the thread type.  This setting will be needed in
		 * up_initial_state() is called.
		 */

		ttype &= TCB_FLAG_TTYPE_MASK;
		tcb->flags &= ~TCB_FLAG_TTYPE_MASK;
		tcb->flags |= ttype;

#ifdef CONFIG_CANCELLATION_POINTS
		/* Set the deferred cancellation type */
		tcb->flags |= TCB_FLAG_CANCEL_DEFERRED;
#endif

		/* Save initial thread scheduling policy int the TCB */

#if CONFIG_RR_INTERVAL > 0
		tcb->flags |= TCB_FLAG_ROUND_ROBIN;
		tcb->timeslice = MSEC2TICK(CONFIG_RR_INTERVAL);
#else
		tcb->flags &= ~TCB_FLAG_ROUND_ROBIN;
#endif

		/* Save the task ID of the parent task in the TCB and allocate
		 * a child status structure.
		 */

		task_saveparent(tcb, ttype);

#ifdef CONFIG_SMP
		/* exec(), task_create(), and vfork() all inherit the affinity mask
		 * from the parent thread.  This is the default for pthread_create()
		 * as well but the affinity mask can be specified in the pthread
		 * attributes as well.  pthread_create() will have to fix up the
		 * affinity mask in this case.
		 */

		task_inherit_affinity(tcb);
#endif

		/* exec(), pthread_create(), task_create(), and vfork() all
		 * inherit the signal mask of the parent thread.
		 */

#ifndef CONFIG_DISABLE_SIGNALS
		(void)sigprocmask(SIG_SETMASK, NULL, &tcb->sigprocmask);
#endif

		/* Initialize the task state.  It does not get a valid state
		 * until it is activated.
		 */

		tcb->task_state = TSTATE_TASK_INVALID;

		/* Clone the parent tasks D-Space (if it was running PIC).  This
		 * must be done before calling up_initial_state() so that the
		 * state setup will take the PIC address base into account.
		 */

		task_dupdspace(tcb);

		/* Initialize the processor-specific portion of the TCB */

		up_initial_state(tcb);

#ifdef CONFIG_APP_BINARY_SEPARATION
		/* Copy some attributes from parent task to this task */
		rtcb = this_task();
		tcb->app_id = rtcb->app_id;

		/* If a kernel thread is getting created due to some
		 * call from a user task or pthread, then the kernel thread
		 * will not inherit the some attributes related to heap, uspace,
		 * memory protection, etc..
		 */
		if ((tcb->flags & TCB_FLAG_TTYPE_MASK) != TCB_FLAG_TTYPE_KERNEL) {
			tcb->uspace = rtcb->uspace;
			tcb->uheap = rtcb->uheap;
#ifdef CONFIG_ARCH_USE_MMU
			tcb->pgtbl = rtcb->pgtbl;
#endif
			/* Copy the MPU register values from parent to child task */
#ifdef CONFIG_ARM_MPU
			/* Copy NUM_APP_REGIONS MPU regions:
			 * - 1 region for normal ELF (NUM_APP_REGIONS=1)
			 * - 2 regions for XIP_ELF (NUM_APP_REGIONS=2)
			 * - 3 regions for OPTIMIZE_APP_RELOAD_TIME (NUM_APP_REGIONS=3)
			 */
			for (int i = 0; i < MPU_REG_NUMBER * NUM_APP_REGIONS; i += MPU_REG_NUMBER) {
				tcb->mpu_regs[i + MPU_REG_RNR] = rtcb->mpu_regs[i + MPU_REG_RNR];
				tcb->mpu_regs[i + MPU_REG_RBAR] = rtcb->mpu_regs[i + MPU_REG_RBAR];
				tcb->mpu_regs[i + MPU_REG_RASR] = rtcb->mpu_regs[i + MPU_REG_RASR];
			}
#endif
		}
#endif

		tcb->fin_data = NO_FIN_DATA;

		/* Add the task to the inactive task list */

		sched_lock();
		dq_addfirst((FAR dq_entry_t *)tcb, (dq_queue_t *)&g_inactivetasks);
		tcb->task_state = TSTATE_TASK_INACTIVE;
		sched_unlock();
	}

	return ret;
}

/****************************************************************************
 * Name: task_abortsetup
 *
 * Description:
 *   Abort a completed task_schedsetup() before task activation.  This removes
 *   the unpublished task from the inactive list and reverses the parent-side
 *   child bookkeeping before the child group, PID, or TCB is released.
 *
 ****************************************************************************/

void task_abortsetup(FAR struct tcb_s *tcb)
{
	irqstate_t flags;

	DEBUGASSERT(tcb != NULL);

	flags = enter_critical_section();
	if (tcb->task_state == TSTATE_TASK_INACTIVE) {
		task_abortparent(tcb);
		sched_removeblocked(tcb);
	}
	leave_critical_section(flags);
}

/****************************************************************************
 * Name: task_namesetup
 *
 * Description:
 *   Assign the task name.
 *
 * Input Parameters:
 *   tcb        - Address of the new task's TCB
 *   name       - Name of the new task
 *
 * Return Value:
 *  None
 *
 ****************************************************************************/

#if CONFIG_TASK_NAME_SIZE > 0
static void task_namesetup(FAR struct task_tcb_s *tcb, FAR const char *name)
{
	/* Give a name to the unnamed tasks */

	if (!name) {
		name = (FAR char *)g_noname;
	}

	/* Copy the name into the TCB */

	strncpy(tcb->cmn.name, name, CONFIG_TASK_NAME_SIZE);
	tcb->cmn.name[CONFIG_TASK_NAME_SIZE] = '\0';
}
#else
#define task_namesetup(t, n)
#endif							/* CONFIG_TASK_NAME_SIZE */

/****************************************************************************
 * Name: task_stackargsetup
 *
 * Description:
 *   This functions is called only from task_argsetup()  It will allocate
 *   space on the new task's stack and will copy the argv[] array and all
 *   strings to the task's stack where it is readily accessible to the
 *   task.  Data on the stack, on the other hand, is guaranteed to be
 *   accessible no matter what privilege mode the task runs in.
 *
 * Input Parameters:
 *   tcb  - Address of the new task's TCB
 *   argv - A pointer to an array of input parameters. The array should be
 *          terminated with a NULL argv[] value. If no parameters are
 *          required, argv may be NULL.
 *
 * Return Value:
 *  Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

static inline int task_stackargsetup(FAR struct task_tcb_s *tcb, FAR char *const argv[])
{
	FAR char **stackargv;
	FAR const char *name;
	FAR char *str;
	FAR char *strstart;
	size_t arglen;
	size_t namelen;
	size_t remaining;
	size_t strtablen;
	size_t argvlen;
	size_t nbytes;
	int argc;
	int i;

	/* Get the name string that we will use as the first argument */

#if CONFIG_TASK_NAME_SIZE > 0
	name = tcb->cmn.name;
#else
	name = (FAR const char *)g_noname;
#endif							/* CONFIG_TASK_NAME_SIZE */

	/* Get the size of the task name (including the NUL terminator) */

	namelen = strlen(name);
	if (namelen == SIZE_MAX) {
		return -EOVERFLOW;
	}

	strtablen = namelen + 1;
	if (strtablen >= tcb->cmn.adj_stack_size) {
		return -ENAMETOOLONG;
	}

	/* Count the number of arguments and get the accumulated size of the
	 * argument strings (including the null terminators).  The argument count
	 * does not include the task name in that will be in argv[0].
	 */

	argc = 0;
	if (argv) {
		/* A NULL argument terminates the list */

		while (argv[argc]) {
			/* Add the size of this argument (with NUL terminator).
			 * Check each time if the accumulated size exceeds the
			 * size of the allocated stack.
			 */

			arglen = strnlen(argv[argc], tcb->cmn.adj_stack_size);
			if (arglen == tcb->cmn.adj_stack_size) {
				return -ENAMETOOLONG;
			}

			if (strtablen > SIZE_MAX - arglen - 1) {
				return -EOVERFLOW;
			}

			strtablen += arglen + 1;
			if (strtablen >= tcb->cmn.adj_stack_size) {
				return -ENAMETOOLONG;
			}

			/* Increment the number of args.  Here is a sanity check to
			 * prevent running away with an unterminated argv[] list.
			 * MAX_STACK_ARGS should be sufficiently large that this never
			 * happens in normal usage.
			 */

			if (++argc > MAX_STACK_ARGS) {
				return -E2BIG;
			}
		}
	}

	/* Allocate a stack frame to hold argv[] array and the strings.  NOTE
	 * that argc + 2 entries are needed:  The number of arguments plus the
	 * task name plus a NULL argv[] entry to terminate the list.
	 */

	if ((size_t)(argc + 2) > SIZE_MAX / sizeof(FAR char *)) {
		return -EOVERFLOW;
	}

	argvlen = (size_t)(argc + 2) * sizeof(FAR char *);
	if (argvlen > SIZE_MAX - strtablen) {
		return -EOVERFLOW;
	}

	stackargv = (FAR char **)up_stack_frame(&tcb->cmn, argvlen + strtablen);

	DEBUGASSERT(stackargv != NULL);
	if (stackargv == NULL) {
		return -ENOMEM;
	}

	/* Get the address of the string table that will lie immediately after
	 * the argv[] array and mark it as a null string.
	 */

	str = (FAR char *)stackargv + argvlen;
	strstart = str;

	/* Copy the task name.  Increment str to skip over the task name and its
	 * NUL terminator in the string buffer.
	 */

	stackargv[0] = str;
	nbytes = namelen + 1;
	memcpy(str, name, nbytes);
	str += nbytes;

	/* Copy each argument */

	for (i = 0; i < argc; i++) {
		/* Save the pointer to the location in the string buffer and copy
		 * the argument into the buffer.  Increment str to skip over the
		 * argument and its NUL terminator in the string buffer.
		 */

		remaining = strtablen - (size_t)(str - strstart);
		if (argv[i] == NULL) {
			return -EOVERFLOW;
		}

		arglen = strnlen(argv[i], remaining);
		if (arglen == remaining) {
			return -EOVERFLOW;
		}

		stackargv[i + 1] = str;
		nbytes = arglen + 1;
		memcpy(str, argv[i], nbytes);
		str += nbytes;
	}

	/* Put a terminator entry at the end of the argv[] array.  Then save the
	 * argv[] arry pointer in the TCB where it will be recovered later by
	 * task_start().
	 */

	stackargv[argc + 1] = NULL;
	tcb->argv = stackargv;

	return OK;
}

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_PROCESS)
int task_cmdline_setup(FAR struct task_tcb_s *tcb)
{
	FAR char *cmdline;
	FAR char *oldcmdline;
	FAR const char *name;
	irqstate_t flags;
	size_t allocsize;
	size_t arglen;
	size_t namelen;
	size_t remaining;
	size_t total;
	size_t used;
	int argc;
	int i;

#if CONFIG_TASK_NAME_SIZE > 0
	name = tcb->cmn.name;
#else
	name = (FAR const char *)g_noname;
#endif

	namelen = strlen(name);
	total = namelen;
	argc = 1;

	if (tcb->argv != NULL) {
		while (tcb->argv[argc] != NULL) {
			if (argc > MAX_STACK_ARGS) {
				return -E2BIG;
			}

			arglen = strlen(tcb->argv[argc]);
			if (total == SIZE_MAX || arglen > SIZE_MAX - total - 1) {
				return -EOVERFLOW;
			}

			total += arglen + 1;
			argc++;
		}
	}

	allocsize = total > 0 ? total : 1;
	cmdline = (FAR char *)kmm_malloc(allocsize);
	if (cmdline == NULL) {
		return -ENOMEM;
	}

	memcpy(cmdline, name, namelen);
	used = namelen;

	for (i = 1; i < argc; i++) {
		remaining = total - used;
		if (tcb->argv[i] == NULL || remaining == 0) {
			kmm_free(cmdline);
			return -EOVERFLOW;
		}

		arglen = strnlen(tcb->argv[i], remaining);
		if (arglen >= remaining) {
			kmm_free(cmdline);
			return -EOVERFLOW;
		}

		cmdline[used++] = ' ';
		memcpy(cmdline + used, tcb->argv[i], arglen);
		used += arglen;
	}

	flags = enter_critical_section();
	oldcmdline = tcb->cmdline;
	tcb->cmdline = cmdline;
	tcb->cmdline_len = used;
	leave_critical_section(flags);

	if (oldcmdline != NULL) {
		kmm_free(oldcmdline);
	}

	return OK;
}

int task_cmdline_clone(FAR struct task_tcb_s *child, FAR struct tcb_s *parent)
{
	FAR struct task_tcb_s *ptcb;
	FAR char *cmdline;
	irqstate_t flags;
	size_t allocsize;

#ifndef CONFIG_DISABLE_PTHREAD
	if ((parent->flags & TCB_FLAG_TTYPE_MASK) == TCB_FLAG_TTYPE_PTHREAD) {
		return task_cmdline_setup(child);
	}
#endif

	ptcb = (FAR struct task_tcb_s *)parent;
	if (ptcb->cmdline == NULL) {
		return task_cmdline_setup(child);
	}

	allocsize = ptcb->cmdline_len > 0 ? ptcb->cmdline_len : 1;
	cmdline = (FAR char *)kmm_malloc(allocsize);
	if (cmdline == NULL) {
		return -ENOMEM;
	}

	memcpy(cmdline, ptcb->cmdline, ptcb->cmdline_len);
	flags = enter_critical_section();
	child->cmdline = cmdline;
	child->cmdline_len = ptcb->cmdline_len;
	leave_critical_section(flags);
	return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: task_schedsetup
 *
 * Description:
 *   This functions initializes a Task Control Block (TCB) in preparation
 *   for starting a new task.
 *
 *   task_schedsetup() is called from task_init() and task_start().
 *
 * Input Parameters:
 *   tcb        - Address of the new task's TCB
 *   priority   - Priority of the new task
 *   start      - Start-up function (probably task_start())
 *   main       - Application start point of the new task
 *   ttype      - Type of the new thread: task or kernel thread
 *
 * Return Value:
 *   OK on success; ERROR on failure.
 *
 *   This function can fail for two reasons.
 *   1) If requested priority is beyond the allowed range, errno = EINVAL
 *   2) If it is unable to assign a new, unique task ID to the TCB. errno = EBUSY
 *   errno is set accodingly.
 *
 ****************************************************************************/

int task_schedsetup(FAR struct task_tcb_s *tcb, int priority, start_t start, main_t main, uint8_t ttype)
{
	int ret;
	/* Perform common thread setup */

	ret = thread_schedsetup((FAR struct tcb_s *)tcb, priority, start, (CODE void *)main, ttype);
	if (ret == OK) {
		/* Save task restart priority */

		tcb->init_priority = (uint8_t)priority;
	}

	return ret;
}

/****************************************************************************
 * Name: pthread_schedsetup
 *
 * Description:
 *   This functions initializes a Task Control Block (TCB) in preparation
 *   for starting a new pthread.
 *
 *   pthread_schedsetup() is called from pthread_create(),
 *
 * Input Parameters:
 *   tcb        - Address of the new task's TCB
 *   priority   - Priority of the new task
 *   start      - Start-up function (probably pthread_start())
 *   entry      - Entry point of the new pthread
 *   ttype      - Type of the new thread: task, pthread, or kernel thread
 *
 * Return Value:
 *   OK on success; ERROR on failure.
 *
 *   This function can fail for two reasons.
 *   1) If requested priority is beyond the allowed range, errno = EINVAL
 *   2) If it is unable to assign a new, unique task ID to the TCB. errno = EBUSY
 *   errno is set accodingly.
 *
 ****************************************************************************/

#ifndef CONFIG_DISABLE_PTHREAD
int pthread_schedsetup(FAR struct pthread_tcb_s *tcb, int priority, start_t start, pthread_startroutine_t entry)
{
	/* Perform common thread setup */

	return thread_schedsetup((FAR struct tcb_s *)tcb, priority, start, (CODE void *)entry, TCB_FLAG_TTYPE_PTHREAD);
}
#endif

/****************************************************************************
 * Name: task_argsetup
 *
 * Description:
 *   This functions sets up parameters in the Task Control Block (TCB) in
 *   preparation for starting a new thread.
 *
 *   task_argsetup() is called only from task_init() and task_start() to
 *   create a new task.  In the "normal" case, the argv[] array is a
 *   structure in the TCB, the arguments are cloned via strdup.
 *
 *   In the kernel build case, the argv[] array and all strings are copied
 *   to the task's stack.  This is done because the TCB (and kernel allocated
 *   strings) are only accessible in kernel-mode.  Data on the stack, on the
 *   other hand, is guaranteed to be accessible no matter what mode the
 *   task runs in.
 *
 * Input Parameters:
 *   tcb        - Address of the new task's TCB
 *   name       - Name of the new task (not used)
 *   argv       - A pointer to an array of input parameters.
 *                The array should be terminated with a NULL argv[] value.
 *                If no parameters are required, argv may be NULL.
 *
 * Return Value:
 *  OK
 *
 ****************************************************************************/

int task_argsetup(FAR struct task_tcb_s *tcb, FAR const char *name, FAR char *const argv[])
{
	int ret;

	/* Setup the task name */

	task_namesetup(tcb, name);

	/* Copy the argv[] array and all strings are to the task's stack.  Data on
	 * the stack is guaranteed to be accessible by the ask no matter what
	 * privilege mode the task runs in.
	 */

	ret = task_stackargsetup(tcb, argv);
	if (ret < 0) {
		return ret;
	}

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_PROCESS)
	return task_cmdline_setup(tcb);
#else
	return OK;
#endif
}
