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
#include <sys/wait.h>
#include <sys/types.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <debug.h>
#include <sched.h>

#include <tinyara/arch.h>
#include <tinyara/kmalloc.h>
#include <tinyara/kthread.h>
#ifdef CONFIG_DEBUG_MM_HEAPINFO
#include <tinyara/mm/mm.h>
#endif
#include <tinyara/sched.h>
#include <tinyara/os_api_test_drv.h>

#include "sched/sched.h"
#include "task/task.h"
#if defined(HAVE_TASK_GROUP) || CONFIG_NFILE_DESCRIPTORS > 0 || CONFIG_NSOCKET_DESCRIPTORS > 0
#include "group/group.h"
#endif
#if defined(CONFIG_BINARY_MANAGER) && defined(CONFIG_APP_BINARY_SEPARATION)
#include "binary_manager/binary_manager_internal.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TEST_STACK_SIZE (2048)
#define TEST_PRIORITY   (100)
#define TEST_TASK_NAME  ("test_taskinit")
#define TEST_LIFECYCLE_TASK_NAME ("test_lifecycle")
#define TEST_RESTART_TASK_NAME   ("test_restart")
#define TEST_STARTHOOK_TASK_NAME ("test_starthook")
#define TEST_TASK_SLEEP_SEC      (1)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static sem_t g_test_task_sem;
static volatile int g_test_task_counter;
#ifdef CONFIG_SCHED_STARTHOOK
static volatile int g_test_starthook_counter;
static volatile int g_test_starthook_sequence;
static FAR void *g_test_starthook_arg;
static FAR void *g_test_starthook_expected;
#endif

/****************************************************************************
 * Private Function
 ****************************************************************************/

static int test_task_lifecycle_entry(int argc, char *argv[])
{
	g_test_task_counter++;
	(void)sem_post(&g_test_task_sem);

	return OK;
}

static int test_task_init_entry(int argc, char *argv[])
{
	g_test_task_counter++;
	(void)sem_post(&g_test_task_sem);

	return OK;
}

static int test_task_restart_entry(int argc, char *argv[])
{
	g_test_task_counter++;
	(void)sem_post(&g_test_task_sem);
	sleep(TEST_TASK_SLEEP_SEC);

	return OK;
}

static int test_task_wait_sem(void)
{
	int ret;

	do {
		ret = sem_wait(&g_test_task_sem);
	} while (ret < 0 && get_errno() == EINTR);

	return ret;
}

/* Wait until the given pid has been fully released from the scheduler.
 * See the SMP note in test_task_lifecycle(): after waitpid() returns, the
 * child may still be running sched_releasetcb() on another CPU. Poll a
 * bounded number of times so a lingering pid does not make the ESRCH check
 * flaky. Returns OK once the pid is gone, ERROR if it never disappears.
 */
#define TEST_TASK_RELEASE_RETRIES	100
static int test_task_wait_released(pid_t pid)
{
	int retry;

	for (retry = 0; retry < TEST_TASK_RELEASE_RETRIES; retry++) {
		if (sched_gettcb(pid) == NULL) {
			return OK;
		}
		usleep(1000);
	}

	return ERROR;
}

static int test_task_lifecycle(unsigned long arg)
{
	int ret = ERROR;
	int status;
	int pid;

	(void)arg;

	set_errno(0);
	pid = kernel_thread(TEST_LIFECYCLE_TASK_NAME, SCHED_PRIORITY_MIN - 1, TEST_STACK_SIZE, test_task_lifecycle_entry, NULL);
	if (pid != ERROR || get_errno() != EINVAL) {
		return ERROR;
	}

	if (sem_init(&g_test_task_sem, 0, 0) != OK) {
		return ERROR;
	}

	g_test_task_counter = 0;
	pid = kernel_thread(TEST_LIFECYCLE_TASK_NAME, TEST_PRIORITY, TEST_STACK_SIZE, test_task_lifecycle_entry, NULL);
	if (pid <= 0) {
		goto errout_with_sem;
	}

#ifdef HAVE_TASK_GROUP
	if (task_getgroup(pid) == NULL) {
		(void)task_delete(pid);
		goto errout_with_sem;
	}
#endif

	if (test_task_wait_sem() != OK || waitpid(pid, &status, 0) != pid || g_test_task_counter != 1) {
		(void)task_delete(pid);
		goto errout_with_sem;
	}

	/* In SMP, waitpid() can return as soon as the child sets CHILD_FLAG_EXITED
	 * in task_exithook(), which runs slightly before sched_releasetcb() frees
	 * the pid on the other CPU. Wait until the pid is actually released so the
	 * ESRCH check below is deterministic rather than racing the child's
	 * teardown.
	 */
	if (test_task_wait_released(pid) != OK) {
		goto errout_with_sem;
	}

	set_errno(0);
	if (task_delete(pid) != ERROR || get_errno() != ESRCH) {
		goto errout_with_sem;
	}

	g_test_task_counter = 0;
	pid = kernel_thread(TEST_RESTART_TASK_NAME, TEST_PRIORITY, TEST_STACK_SIZE, test_task_restart_entry, NULL);
	if (pid <= 0) {
		goto errout_with_sem;
	}

	if (test_task_wait_sem() != OK || g_test_task_counter != 1) {
		(void)task_delete(pid);
		goto errout_with_sem;
	}

	if (task_restart(pid) != OK) {
		(void)task_delete(pid);
		goto errout_with_sem;
	}

	if (test_task_wait_sem() != OK || waitpid(pid, &status, 0) != pid || g_test_task_counter != 2) {
		(void)task_delete(pid);
		goto errout_with_sem;
	}

	ret = OK;

errout_with_sem:
	sem_destroy(&g_test_task_sem);
	return ret;
}

static int test_task_reparent(unsigned long arg)
{
#ifdef CONFIG_SCHED_HAVE_PARENT
	int ret;
	int pid;
	int before_parent_id = 0;
	int after_parent_id = 0;
	struct tcb_s *child_tcb;

	pid = getpid();
	child_tcb = sched_gettcb(pid);
	if (child_tcb == NULL) {
		return ERROR;
	}
#ifdef HAVE_GROUP_MEMBERS
	before_parent_id = child_tcb->group->tg_pgid;
#else
	before_parent_id = child_tcb->group->tg_ppid;
#endif

	ret = task_reparent((int)arg, pid);
	if (ret != OK) {
		return ERROR;
	}

#ifdef HAVE_GROUP_MEMBERS
	after_parent_id = child_tcb->group->tg_pgid;
#else
	after_parent_id = child_tcb->group->tg_ppid;
#endif
	if (before_parent_id == after_parent_id) {
		return ERROR;
	}
#endif
	return OK;
}

static int test_task_init(main_t entry, FAR pid_t *created_pid)
{
	struct task_tcb_s *tcb;
	uint32_t *stack;
	pid_t pid;
	int ret;

	tcb = (struct task_tcb_s *)kmm_zalloc(sizeof(struct task_tcb_s));
	if (!tcb) {
		berr("Failed: no memory for tcb\n");
		return -ENOMEM;
	}

	stack = (uint32_t *)kumm_malloc(TEST_STACK_SIZE);
	if (!stack) {
		berr("Failed: no memory for stack\n");
		ret = -ENOMEM;
		goto errout_with_tcb;
	}

	/* positive test */

	ret = task_init((struct tcb_s *)tcb, (const char *)TEST_TASK_NAME, (int)TEST_PRIORITY,
			stack, TEST_STACK_SIZE, entry, NULL);
	if (ret < 0) {
		berr("Failed: task_init %d\n", ret);
		ret = -get_errno();
		goto errout_with_stack;
	}

	/* Check the TCB values */

	if ((tcb->cmn.sched_priority != TEST_PRIORITY) ||
		(tcb->cmn.stack_alloc_ptr != stack) ||
		(tcb->cmn.entry.main != entry)) {
		berr("Failed: set values, %d, %x, %x %d\n", tcb->cmn.pid, tcb->cmn.stack_alloc_ptr, tcb->cmn.entry.main);
		ret = -ENXIO;
		goto errout_with_task;
	}

	pid = tcb->cmn.pid;
	ret = task_activate((FAR struct tcb_s *)tcb);
	if (ret < 0) {
		berr("Failed : task_activate() %d\n", ret);
		ret = -get_errno();
		goto errout_with_task;
	}

	if (created_pid != NULL) {
		*created_pid = pid;
	}

	return OK;

errout_with_task:
	sched_removeblocked((struct tcb_s *)tcb);
	sched_releasetcb(&tcb->cmn, TCB_FLAG_TTYPE_TASK);
	return ret;

errout_with_stack:
	kumm_free(stack);

errout_with_tcb:
	kmm_free(tcb);
	return ret;
}

static int test_task_init_internal(unsigned long arg)
{
	struct task_tcb_s *tcb;
	uint32_t *stack;
	int ret;

	(void)arg;

	tcb = (struct task_tcb_s *)kmm_zalloc(sizeof(struct task_tcb_s));
	if (!tcb) {
		return ERROR;
	}

	stack = (uint32_t *)kumm_malloc(TEST_STACK_SIZE);
	if (!stack) {
		kmm_free(tcb);
		return ERROR;
	}

	ret = task_init((struct tcb_s *)tcb, TEST_TASK_NAME, TEST_PRIORITY, stack, TEST_STACK_SIZE, test_task_init_entry, NULL);
	if (ret < 0) {
		kumm_free(stack);
		kmm_free(tcb);
		return ERROR;
	}

	if (tcb->cmn.sched_priority != TEST_PRIORITY ||
		tcb->cmn.stack_alloc_ptr != stack ||
		tcb->cmn.entry.main != test_task_init_entry) {
		berr("Failed: task_init set incorrect TCB values\n");
		ret = ERROR;
	}

	/* Release TCB without activating: entry is a kernel function and
	 * cannot run in user mode under CONFIG_BUILD_PROTECTED. */
	sched_removeblocked((struct tcb_s *)tcb);
	sched_releasetcb(&tcb->cmn, TCB_FLAG_TTYPE_TASK);
	return ret;
}

#ifdef CONFIG_SCHED_STARTHOOK
static void test_task_starthook_callback(FAR void *arg)
{
	g_test_starthook_counter++;
	g_test_starthook_arg = arg;
	if (g_test_starthook_sequence == 0) {
		g_test_starthook_sequence = 1;
	} else {
		g_test_starthook_sequence = ERROR;
	}
}

static int test_task_starthook_entry(int argc, char *argv[])
{
	if (g_test_starthook_counter == 1 &&
		g_test_starthook_arg == g_test_starthook_expected &&
		g_test_starthook_sequence == 1) {
		g_test_starthook_sequence = 2;
		g_test_task_counter++;
	} else {
		g_test_starthook_sequence = ERROR;
	}

	(void)sem_post(&g_test_task_sem);
	return OK;
}

static int test_task_create_kernel_with_starthook(main_t entry, starthook_t starthook, FAR void *arg)
{
	FAR struct task_tcb_s *tcb;
	pid_t pid;
	int errcode;
	int ret;

	tcb = (FAR struct task_tcb_s *)kmm_zalloc(sizeof(struct task_tcb_s));
	if (!tcb) {
		errcode = ENOMEM;
		goto errout;
	}

#ifdef HAVE_TASK_GROUP
	ret = group_allocate(tcb, TCB_FLAG_TTYPE_KERNEL);
	if (ret < 0) {
		errcode = -ret;
		goto errout_with_tcb;
	}
#endif

#if CONFIG_NFILE_DESCRIPTORS > 0 || CONFIG_NSOCKET_DESCRIPTORS > 0
	ret = group_setuptaskfiles(tcb);
	if (ret < OK) {
		errcode = -ret;
		goto errout_with_tcb;
	}
#endif

	ret = up_create_stack((FAR struct tcb_s *)tcb, TEST_STACK_SIZE, TCB_FLAG_TTYPE_KERNEL);
	if (ret < OK) {
		errcode = -ret;
		goto errout_with_tcb;
	}

	ret = task_schedsetup(tcb, TEST_PRIORITY, task_start, entry, TCB_FLAG_TTYPE_KERNEL);
	if (ret < OK) {
		errcode = get_errno();
		goto errout_with_tcb;
	}

#ifdef CONFIG_DEBUG_MM_HEAPINFO
	heapinfo_exclude_stacksize(tcb->cmn.stack_alloc_ptr);
	heapinfo_set_stack_node(tcb->cmn.stack_alloc_ptr, tcb->cmn.pid);
#endif

	(void)task_argsetup(tcb, TEST_STARTHOOK_TASK_NAME, NULL);

#ifdef HAVE_TASK_GROUP
	ret = group_initialize(tcb);
	if (ret < 0) {
		errcode = -ret;
		goto errout_with_tcb;
	}
#endif

	pid = (int)tcb->cmn.pid;
#if defined(CONFIG_BINARY_MANAGER) && defined(CONFIG_APP_BINARY_SEPARATION)
	FAR struct tcb_s *rtcb = this_task();
	tcb->cmn.group->tg_binidx = rtcb->group->tg_binidx;
	binary_manager_add_binlist(&tcb->cmn);
#endif
	task_starthook(tcb, starthook, arg);
	(void)task_activate((FAR struct tcb_s *)tcb);
	return pid;

errout_with_tcb:
	sched_releasetcb((FAR struct tcb_s *)tcb, TCB_FLAG_TTYPE_KERNEL);

errout:
	set_errno(errcode);
	return ERROR;
}

static int test_task_starthook(unsigned long arg)
{
	struct task_tcb_s tcb;
	int ret = ERROR;
	int status;
	int marker;
	int pid;

	(void)arg;
	memset(&tcb, 0, sizeof(struct task_tcb_s));

	g_test_starthook_counter = 0;
	g_test_starthook_sequence = 0;
	g_test_starthook_arg = NULL;
	g_test_starthook_expected = &marker;
	marker = 0;

	task_starthook(&tcb, test_task_starthook_callback, &marker);
	if (tcb.starthook != test_task_starthook_callback || tcb.starthookarg != &marker) {
		return ERROR;
	}

	if (sem_init(&g_test_task_sem, 0, 0) != OK) {
		return ERROR;
	}

	g_test_task_counter = 0;
	pid = test_task_create_kernel_with_starthook(test_task_starthook_entry, test_task_starthook_callback, &marker);
	if (pid <= 0) {
		goto errout_with_sem;
	}

	if (test_task_wait_sem() != OK) {
		(void)task_delete(pid);
		goto errout_with_sem;
	}

	if (waitpid(pid, &status, 0) != pid) {
		(void)task_delete(pid);
		goto errout_with_sem;
	}

	if (g_test_starthook_counter == 1 &&
		g_test_starthook_arg == &marker &&
		g_test_starthook_sequence == 2 &&
		g_test_task_counter == 1) {
		ret = OK;
	}

errout_with_sem:
	sem_destroy(&g_test_task_sem);
	return ret;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_task(int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	switch (cmd) {
	case TESTIOC_TASK_REPARENT:
		ret = test_task_reparent(arg);
		break;
	case TESTIOC_TASK_INIT_TEST:
		if (arg == 0) {
			ret = test_task_init_internal(arg);
		} else {
			ret = test_task_init((main_t)arg, NULL);
		}
		break;
	case TESTIOC_TASK_LIFECYCLE_TEST:
		ret = test_task_lifecycle(arg);
		break;
#ifdef CONFIG_SCHED_STARTHOOK
	case TESTIOC_TASK_STARTHOOK_TEST:
		ret = test_task_starthook(arg);
		break;
#endif
	}
	return ret;
}
