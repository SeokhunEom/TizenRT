/****************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
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
#include <sys/types.h>
#include <debug.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>

#include <tinyara/irq.h>
#include <tinyara/kmalloc.h>
#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>
#ifdef CONFIG_SCHED_HAVE_PARENT
#include "group/group.h"
#ifndef CONFIG_DISABLE_SIGNALS
#include "signal/signal.h"
#endif
#ifdef CONFIG_SCHED_CHILD_STATUS
#include "task/task.h"
#endif
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if defined(CONFIG_SCHED_HAVE_PARENT) && defined(CONFIG_SCHED_CHILD_STATUS)
#define TASK_STACKSIZE 2048
#define TEST_GROUP_CHILD_WAIT_RETRY 100
#define TEST_GROUP_CHILD_WAIT_USEC 1000
#ifndef CONFIG_DISABLE_SIGNALS
#define TEST_GROUP_SIGNAL_SIGNO SIGUSR2
#define TEST_GROUP_SIGNAL_VALUE 0x47525053

struct group_sigmask_state_s {
	FAR struct tcb_s *tcb;
	sigset_t sigprocmask;
};
#endif
#endif

/****************************************************************************
 * Private Function
 ****************************************************************************/

#if defined(CONFIG_SCHED_HAVE_PARENT) && defined(CONFIG_SCHED_CHILD_STATUS)
static int get_group(struct tcb_s **rtcb, struct task_group_s **group)
{
	*rtcb = sched_self();
	if (*rtcb == NULL) {
		dbg("sched_self failed.");
		return ERROR;
	} else if ((*rtcb)->group == NULL) {
		dbg("group is null.");
		return ERROR;
	}
	*group = (*rtcb)->group;

	return OK;
}

static int group_exitchild_func(int argc, char *argv[])
{
	task_delete(0);
	return ERROR;
}

static int test_group_wait_exited_child(FAR struct task_group_s *group, pid_t pid, FAR struct child_status_s **child)
{
	int retry;

	for (retry = 0; retry < TEST_GROUP_CHILD_WAIT_RETRY; retry++) {
		*child = group_findchild(group, pid);
		if (*child != NULL && ((*child)->ch_flags & CHILD_FLAG_EXITED) != 0) {
			return OK;
		}

		usleep(TEST_GROUP_CHILD_WAIT_USEC);
	}

	return ERROR;
}

static int test_group_exit_child(unsigned long arg)
{
	static struct tcb_s *rtcb;
	static struct task_group_s *group;
	static struct child_status_s *child;
	static struct child_status_s *child_returned;
	static pid_t child_pid;
	if (get_group(&rtcb, &group) == ERROR) {
		return ERROR;
	}

	child_pid = kernel_thread("group", SCHED_PRIORITY_DEFAULT, TASK_STACKSIZE, group_exitchild_func, (char *const *)NULL);
	if (child_pid < 0) {
		dbg("kernel_thread failed.");
		return ERROR;
	}

	if (test_group_wait_exited_child(group, child_pid, &child) != OK) {
		dbg("child did not exit.");
		(void)task_delete(child_pid);
		if (test_group_wait_exited_child(group, child_pid, &child) == OK) {
			child_returned = group_removechild(group, child_pid);
			if (child_returned != NULL) {
				group_freechild(child_returned);
			}
		}
		return ERROR;
	}

	child_returned = group_exitchild(group);
	if (child != child_returned) {
		dbg("group_exitchild failed.");
		goto errout_with_child;
	}

	child_returned = group_removechild(group, child_pid);
	if (child != child_returned) {
		dbg("group_removechild failed.");
		goto errout_with_child;
	}

	group_freechild(child);
	return OK;

errout_with_child:
	child_returned = group_removechild(group, child_pid);
	if (child_returned != NULL) {
		group_freechild(child_returned);
	}

	return ERROR;
}

static int test_group_add_fined_remove(unsigned long arg)
{
	struct task_group_s group;
	static struct child_status_s *child;
	static struct child_status_s *child_returned;
	static pid_t child_pid;

	(void)arg;
	memset(&group, 0, sizeof(struct task_group_s));

	child = group_allocchild();
	if (child == NULL) {
		dbg("group_allocchild failed.");
		return ERROR;
	}

	child_pid = -1;
	child->ch_flags = TCB_FLAG_TTYPE_TASK;
	child->ch_pid = child_pid;
	child->ch_status = 0;
	/* Add the entry into the TCB list of children */
	group_addchild(&group, child);

	/* cross check starts */
	child_returned = group_findchild(&group, child_pid);
	if (child != child_returned) {
		dbg("group_findchild failed.");
		goto errout_with_addchild;
	}

	child_returned = group_removechild(&group, child_pid);
	if (child != child_returned) {
		dbg("group_removechild failed.");
		goto errout_with_addchild;
	}

	child_returned = group_findchild(&group, child_pid);
	if (child_returned != NULL) {
		dbg("group_removechild failed.");
		goto errout_with_addchild;
	}
	group_freechild(child);
	return OK;

errout_with_addchild:
	group_removechild(&group, child_pid);
	group_freechild(child);
	return ERROR;
}

static int test_group_alloc_free(unsigned long arg)
{
	static struct tcb_s *rtcb;
	static struct task_group_s *group;
	static struct child_status_s *child;
	static struct child_status_s *child_returned;
	struct child_status_s child_dummy;
	if (get_group(&rtcb, &group) == ERROR) {
		return ERROR;
	}

	child = group_allocchild();
	if (child == NULL) {
		dbg("group_allocchild failed.");
		return ERROR;
	}
	if (child->flink != NULL) {
		dbg("group_allocchild failed.");
		group_freechild(child);
		return ERROR;
	}

	child->flink = &child_dummy;
	group_freechild(child);
	child_returned = group_allocchild();
	if (child_returned != child || child_returned->flink != NULL) {
		dbg("group_freechild failed.");
		if (child_returned != NULL) {
			group_freechild(child_returned);
		}
		return ERROR;
	}

	group_freechild(child_returned);
	return OK;
}

static int test_group_removechildren(unsigned long arg)
{
	struct task_group_s group;
	static struct child_status_s *child;
	static struct child_status_s *child_returned;
	static pid_t child_pid;

	(void)arg;
	memset(&group, 0, sizeof(struct task_group_s));

	child = group_allocchild();
	if (child == NULL) {
		dbg("group_allocchild failed.");
		return ERROR;
	}

	child_pid = -1;
	child->ch_flags = TCB_FLAG_TTYPE_TASK;
	child->ch_pid = child_pid;
	child->ch_status = 0;
	/* Add the entry into the TCB list of children */
	group_addchild(&group, child);

	/* cross check starts */
	child_returned = group_findchild(&group, child_pid);
	if (child != child_returned) {
		dbg("group_findchild failed.");
		goto errout_with_addchild;
	}

	group_removechildren(&group);
	if (group.tg_children != NULL) {
		dbg("group_removechildren failed.");
		return ERROR;
	}
	return OK;

errout_with_addchild:
	group_removechildren(&group);
	return ERROR;
}

#ifndef CONFIG_DISABLE_SIGNALS
static int test_group_signal_block_members(FAR struct task_group_s *group,
		FAR struct group_sigmask_state_s *states, int count)
{
	irqstate_t flags;
	int i;

	flags = enter_critical_section();
	for (i = 0; i < count; i++) {
		states[i].tcb = sched_gettcb(group->tg_members[i]);
		if (states[i].tcb == NULL) {
			leave_critical_section(flags);
			return ERROR;
		}

		states[i].sigprocmask = states[i].tcb->sigprocmask;
		states[i].tcb->sigprocmask |= SIGNO2SET(TEST_GROUP_SIGNAL_SIGNO);
	}

	leave_critical_section(flags);
	return OK;
}

static void test_group_signal_restore_members(FAR struct group_sigmask_state_s *states, int count)
{
	irqstate_t flags;
	int i;

	flags = enter_critical_section();
	for (i = 0; i < count; i++) {
		if (states[i].tcb != NULL) {
			states[i].tcb->sigprocmask = states[i].sigprocmask;
		}
	}

	leave_critical_section(flags);
}

static int test_group_signal(unsigned long arg)
{
	FAR struct group_sigmask_state_s *states;
	FAR sigpendq_t *pending;
	struct task_group_s *group;
	struct tcb_s *rtcb;
	siginfo_t info;
	int count;
	int ret = ERROR;

	(void)arg;

	if (get_group(&rtcb, &group) == ERROR) {
		return ERROR;
	}

	if ((sig_pendingset(rtcb) & SIGNO2SET(TEST_GROUP_SIGNAL_SIGNO)) != 0) {
		dbg("test signal is already pending.\n");
		return ERROR;
	}

	count = group->tg_nmembers;
	if (count <= 0) {
		dbg("group has no members.\n");
		return ERROR;
	}

	states = (FAR struct group_sigmask_state_s *)kmm_malloc(count * sizeof(struct group_sigmask_state_s));
	if (states == NULL) {
		dbg("group signal state allocation failed.\n");
		return ERROR;
	}

	memset(states, 0, count * sizeof(struct group_sigmask_state_s));
	if (test_group_signal_block_members(group, states, count) != OK) {
		dbg("failed to block group members.\n");
		goto errout_with_states;
	}

	memset(&info, 0, sizeof(info));
	info.si_signo = TEST_GROUP_SIGNAL_SIGNO;
	info.si_code = SI_QUEUE;
	info.si_value.sival_int = TEST_GROUP_SIGNAL_VALUE;

	if (group_signal(group, &info) != OK) {
		dbg("group_signal failed.\n");
		goto errout_with_restore;
	}

	if ((sig_pendingset(rtcb) & SIGNO2SET(TEST_GROUP_SIGNAL_SIGNO)) == 0) {
		dbg("group_signal did not add a pending signal.\n");
		goto errout_with_restore;
	}

	pending = sig_removependingsignal(rtcb, TEST_GROUP_SIGNAL_SIGNO);
	if (pending == NULL) {
		dbg("pending group signal was not removable.\n");
		goto errout_with_restore;
	}

	if (pending->info.si_signo != TEST_GROUP_SIGNAL_SIGNO ||
			pending->info.si_code != SI_QUEUE ||
			pending->info.si_value.sival_int != TEST_GROUP_SIGNAL_VALUE) {
		dbg("pending group signal payload is invalid.\n");
		sig_releasependingsignal(pending);
		goto errout_with_restore;
	}

	sig_releasependingsignal(pending);
	ret = OK;

errout_with_restore:
	pending = sig_removependingsignal(rtcb, TEST_GROUP_SIGNAL_SIGNO);
	if (pending != NULL) {
		sig_releasependingsignal(pending);
	}

	test_group_signal_restore_members(states, count);

errout_with_states:
	kmm_free(states);
	return ret;
}
#endif
#endif
/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_group(int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	switch (cmd) {
#if defined(CONFIG_SCHED_HAVE_PARENT) && defined(CONFIG_SCHED_CHILD_STATUS)
	case TESTIOC_GROUP_ADD_FINED_REMOVE_TEST:
		ret = test_group_add_fined_remove(arg);
		break;
	case TESTIOC_GROUP_ALLOC_FREE_TEST:
		ret = test_group_alloc_free(arg);
		break;
	case TESTIOC_GROUP_EXIT_CHILD_TEST:
		ret = test_group_exit_child(arg);
		break;
	case TESTIOC_GROUP_REMOVECHILDREN_TEST:
		ret = test_group_removechildren(arg);
		break;
#ifndef CONFIG_DISABLE_SIGNALS
	case TESTIOC_GROUP_SIGNAL_TEST:
		ret = test_group_signal(arg);
		break;
#endif
#endif
	}
	return ret;
}
