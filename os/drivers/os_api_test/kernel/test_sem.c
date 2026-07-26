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
#include <errno.h>
#include <debug.h>
#include <stdbool.h>
#include <time.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>

#include <tinyara/irq.h>
#include <tinyara/kthread.h>
#include <tinyara/semaphore.h>
#include <tinyara/sched.h>
#include <tinyara/os_api_test_drv.h>

#include "clock/clock.h"
#include "semaphore/semaphore.h"

/****************************************************************************
 * Private Function
 ****************************************************************************/

struct sem_waiter_context_s {
	FAR sem_t *sem;
	volatile int result;
	volatile bool acquired;
	volatile bool release;
	volatile bool released;
	volatile bool terminate;
};

static struct sem_waiter_context_s g_sem_waiter_context;
static sem_t g_sem_waiter_sem;

#define TEST_SEM_CHILD_WAIT_RETRIES 2000

static int test_sem_waiter(int argc, FAR char *argv[])
{
	FAR struct sem_waiter_context_s *context = &g_sem_waiter_context;

	(void)argc;
	(void)argv;

	context->result = sem_wait(context->sem);
#ifdef SAVE_SEM_HOLDER
	if (context->result == OK &&
		sem_findholder(context->sem, sched_self()) == NULL) {
		context->result = ERROR;
	}
#endif
	context->acquired = true;
	while (!context->release) {
		usleep(1000);
	}
	if (context->result == OK) {
		context->result = sem_post(context->sem);
	}
	context->released = true;
	while (!context->terminate) {
		usleep(1000);
	}

	return context->result;
}

static int test_sem_poster(int argc, FAR char *argv[])
{
	(void)argc;
	(void)argv;

	g_sem_waiter_context.result = sem_post(g_sem_waiter_context.sem);
	return g_sem_waiter_context.result;
}

static int test_sem_wait_child(pid_t pid)
{
	int retry;

	for (retry = 0; retry < TEST_SEM_CHILD_WAIT_RETRIES; retry++) {
		if (sched_gettcb(pid) == NULL) {
			return OK;
		}
		usleep(1000);
	}

	return ERROR;
}

static void test_sem_stop_child(pid_t pid)
{
	int retry;

	(void)task_delete(pid);
	for (retry = 0; retry < TEST_SEM_CHILD_WAIT_RETRIES; retry++) {
		if (sched_gettcb(pid) == NULL) {
			return;
		}
		usleep(1000);
	}
}

static int test_sem_tick_wait(unsigned long arg)
{
	int ret_chk;
	sem_t sem;
	struct timespec cur_time;
	struct timespec base_time;

	(void)arg;

	/* init sem count to 1 */

	ret_chk = sem_init(&sem, 0, 1);
	if (ret_chk != OK) {
		dbg("sem_init failed.");
		return ERROR;
	}

	/* success to get sem case test */

	ret_chk = clock_gettime(CLOCK_REALTIME, &base_time);
	if (ret_chk != OK) {
		dbg("clock_gettime failed.");
		goto errout_with_sem_init;
	}

	ret_chk = sem_tickwait(&sem, clock(), 2);
	if (ret_chk != OK) {
		dbg("sem_tickwait failed.");
		goto errout_with_sem_init;
	}

	ret_chk = clock_gettime(CLOCK_REALTIME, &cur_time);
	if (ret_chk != OK) {
		dbg("clock_gettime failed.");
		goto errout_with_sem_init;
	}
	if (base_time.tv_sec + 2 == cur_time.tv_sec) {
		dbg("clock_gettime failed.");
		goto errout_with_sem_init;
	}

	ret_chk = sem_post(&sem);
	if (ret_chk != OK) {
		dbg("sem_post failed.");
		goto errout_with_sem_init;
	}

	ret_chk = sem_destroy(&sem);
	if (ret_chk != OK) {
		dbg("sem_destroy failed.");
		goto errout_with_sem_init;
	}

	/* init sem count to 0 */

	ret_chk = sem_init(&sem, 0, 0);
	if (ret_chk != OK) {
		dbg("sem_init failed.");
		return ERROR;
	}

	/* expired time test */

	ret_chk = sem_tickwait(&sem, clock() - 2, 0);
	if (ret_chk != ERROR) {
		dbg("sem_tickwait failed.");
		goto errout_with_sem_init;
	}

	ret_chk = sem_tickwait(&sem, clock() - 2, 1);
	if (ret_chk != ERROR) {
		dbg("sem_tickwait failed.");
		goto errout_with_sem_init;
	}

	ret_chk = sem_tickwait(&sem, clock() - 2, 3);
	if (ret_chk != ERROR) {
		dbg("sem_tickwait failed.");
		goto errout_with_sem_init;
	}

	ret_chk = sem_destroy(&sem);
	if (ret_chk != OK) {
		dbg("sem_destroy failed.");
		goto errout_with_sem_init;
	}

	return OK;

errout_with_sem_init:
	sem_destroy(&sem);
	return ERROR;
}

static int test_sem_reset(unsigned long arg)
{
	sem_t sem;

	(void)arg;

	set_errno(0);
	if (sem_reset(NULL, 0) != ERROR || get_errno() != EINVAL) {
		dbg("sem_reset accepted NULL semaphore.\n");
		return ERROR;
	}

	memset(&sem, 0, sizeof(sem));
	set_errno(0);
	if (sem_reset(&sem, 1) != ERROR || get_errno() != EINVAL) {
		dbg("sem_reset accepted uninitialized semaphore.\n");
		return ERROR;
	}

	if (sem_init(&sem, 0, 2) != OK) {
		dbg("sem_init failed.\n");
		return ERROR;
	}

	set_errno(0);
	if (sem_reset(&sem, -1) != ERROR || get_errno() != EINVAL) {
		dbg("sem_reset accepted negative count.\n");
		goto errout_with_sem_init;
	}

	if (sem_reset(&sem, 5) != OK || sem.semcount != 5) {
		dbg("sem_reset did not update semaphore count.\n");
		goto errout_with_sem_init;
	}

	if (sem_reset(&sem, 0) != OK || sem.semcount != 0) {
		dbg("sem_reset did not reset semaphore count.\n");
		goto errout_with_sem_init;
	}

	if (sem_destroy(&sem) != OK) {
		dbg("sem_destroy failed.\n");
		return ERROR;
	}

	return OK;

errout_with_sem_init:
	sem_destroy(&sem);
	return ERROR;
}

static int test_sem_recover(unsigned long arg)
{
	struct tcb_s tcb;
	sem_t sem;

	(void)arg;

	if (sem_init(&sem, 0, 0) != OK) {
		dbg("sem_init failed.\n");
		return ERROR;
	}

	sem.semcount = -2;
	memset(&tcb, 0, sizeof(tcb));
	tcb.task_state = TSTATE_WAIT_SEM;
	tcb.waitsem = &sem;

	sem_recover(&tcb);
	if (sem.semcount != -1 || tcb.waitsem != NULL) {
		dbg("sem_recover did not release a waiting semaphore count.\n");
		goto errout_with_sem_init;
	}

	memset(&tcb, 0, sizeof(tcb));
	tcb.task_state = TSTATE_TASK_RUNNING;
	tcb.waitsem = &sem;

	sem_recover(&tcb);
	if (sem.semcount != -1 || tcb.waitsem != &sem) {
		dbg("sem_recover changed non-waiting task state.\n");
		goto errout_with_sem_init;
	}

	sem.semcount = 0;
	if (sem_destroy(&sem) != OK) {
		dbg("sem_destroy failed.\n");
		return ERROR;
	}

#ifdef CONFIG_PRIORITY_INHERITANCE
	irqstate_t flags;

	/* Model a task that exits after obtaining two counts from one
	 * semaphore.  sem_recover() must return both counts and remove the
	 * holder from the task list.
	 */

	if (sem_init(&sem, 0, 2) != OK) {
		dbg("sem_init for holder recovery failed.\n");
		return ERROR;
	}

	memset(&tcb, 0, sizeof(tcb));
	flags = enter_critical_section();
	sem_addholder_tcb(&tcb, &sem);
	sem_addholder_tcb(&tcb, &sem);
	sem.semcount = 0;
	leave_critical_section(flags);

	sem_recover(&tcb);
	if (sem.semcount != 2 || tcb.holdsem != NULL ||
		sem_findholder(&sem, &tcb) != NULL) {
		dbg("sem_recover did not release every held count.\n");
		goto errout_with_sem_init;
	}

	if (sem_destroy(&sem) != OK) {
		dbg("sem_destroy after holder recovery failed.\n");
		return ERROR;
	}
#endif

	return OK;

errout_with_sem_init:
	sem.semcount = 0;
	sem_destroy(&sem);
	return ERROR;
}

static int test_sem_protocol(unsigned long arg)
{
	(void)arg;

	sem_t sem;

	set_errno(0);
	if (sem_setprotocol(NULL, SEM_PRIO_NONE) != ERROR || get_errno() != EINVAL) {
		dbg("sem_setprotocol accepted NULL semaphore.\n");
		return ERROR;
	}

	memset(&sem, 0, sizeof(sem));
	set_errno(0);
	if (sem_setprotocol(&sem, SEM_PRIO_NONE) != ERROR || get_errno() != EINVAL) {
		dbg("sem_setprotocol accepted uninitialized semaphore.\n");
		return ERROR;
	}

	if (sem_init(&sem, 0, 1) != OK) {
		dbg("sem_init failed.\n");
		return ERROR;
	}

	set_errno(0);
	if (sem_setprotocol(&sem, SEM_PRIO_PROTECT) != ERROR || get_errno() != ENOSYS) {
		dbg("sem_setprotocol accepted unsupported protect protocol.\n");
		goto errout_with_sem_init;
	}

	set_errno(0);
	if (sem_setprotocol(&sem, -1) != ERROR || get_errno() != EINVAL) {
		dbg("sem_setprotocol accepted invalid protocol.\n");
		goto errout_with_sem_init;
	}

	if (sem_setprotocol(&sem, SEM_PRIO_NONE) != OK) {
		dbg("sem_setprotocol failed to disable priority inheritance.\n");
		goto errout_with_sem_init;
	}

#ifdef CONFIG_PRIORITY_INHERITANCE
	if ((sem.flags & PRIOINHERIT_FLAGS_DISABLE) == 0) {
		dbg("sem_setprotocol did not update the protocol flag.\n");
		goto errout_with_sem_init;
	}

	if (sem_setprotocol(&sem, SEM_PRIO_INHERIT) != OK ||
		(sem.flags & PRIOINHERIT_FLAGS_DISABLE) != 0) {
		dbg("sem_setprotocol failed to enable priority inheritance.\n");
		goto errout_with_sem_init;
	}
#else
	set_errno(0);
	if (sem_setprotocol(&sem, SEM_PRIO_INHERIT) != ERROR || get_errno() != ENOSYS) {
		dbg("sem_setprotocol accepted unsupported inherit protocol.\n");
		goto errout_with_sem_init;
	}
#endif

	if (sem_destroy(&sem) != OK) {
		dbg("sem_destroy failed.\n");
		return ERROR;
	}

	return OK;

errout_with_sem_init:
	sem_destroy(&sem);
	return ERROR;
}

static int test_sem_holder(unsigned long arg)
{
	(void)arg;

#ifdef SAVE_SEM_HOLDER
	FAR struct semholder_s *holder;
	FAR struct tcb_s *self;
	sem_t sem;

	self = sched_self();
	if (self == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	if (sem_init(&sem, 0, 1) != OK) {
		dbg("sem_init failed.\n");
		return ERROR;
	}

	if (sem_wait(&sem) != OK) {
		dbg("sem_wait failed.\n");
		goto errout_with_sem_init;
	}

	holder = sem_findholder(&sem, self);
	if (holder == NULL || holder->counts != 1) {
		dbg("sem_wait did not register the current task holder.\n");
		goto errout_with_post;
	}

	if (sem_post(&sem) != OK) {
		dbg("sem_post failed.\n");
		goto errout_with_sem_init;
	}

	if (sem_findholder(&sem, self) != NULL) {
		dbg("sem_post did not release the current task holder.\n");
		goto errout_with_sem_init;
	}

	if (sem_destroy(&sem) != OK) {
		dbg("sem_destroy failed.\n");
		return ERROR;
	}

	return OK;

errout_with_post:
	sem_post(&sem);
errout_with_sem_init:
	sem_destroy(&sem);
	return ERROR;
#else
	return OK;
#endif
}

static int test_sem_holder_multiple_counts(unsigned long arg)
{
	(void)arg;

#ifdef SAVE_SEM_HOLDER
	FAR struct semholder_s *holder;
	FAR struct tcb_s *self;
	sem_t sem;
	int held_counts = 0;
	int ret = ERROR;

	self = sched_self();
	if (self == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	if (sem_init(&sem, 0, 2) != OK) {
		dbg("sem_init failed.\n");
		return ERROR;
	}

	if (sem_wait(&sem) != OK) {
		dbg("first sem_wait failed.\n");
		goto errout_with_sem_init;
	}
	held_counts++;

	if (sem_wait(&sem) != OK) {
		dbg("second sem_wait failed.\n");
		goto errout_with_sem_init;
	}
	held_counts++;

	holder = sem_findholder(&sem, self);
	if (holder == NULL || holder->counts != 2) {
		dbg("multiple waits did not preserve holder counts.\n");
		goto errout_with_sem_init;
	}

	if (sem_post(&sem) != OK) {
		dbg("first sem_post failed.\n");
		goto errout_with_sem_init;
	}
	held_counts--;

	holder = sem_findholder(&sem, self);
	if (holder == NULL || holder->counts != 1) {
		dbg("partial release removed a holder with remaining counts.\n");
		goto errout_with_sem_init;
	}

	if (sem_post(&sem) != OK) {
		dbg("second sem_post failed.\n");
		goto errout_with_sem_init;
	}
	held_counts--;

	if (sem_findholder(&sem, self) != NULL) {
		dbg("final release did not remove the holder.\n");
		goto errout_with_sem_init;
	}

	ret = OK;

errout_with_sem_init:
	while (held_counts > 0) {
		sem_post(&sem);
		held_counts--;
	}
	sem_destroy(&sem);
	return ret;
#else
	return OK;
#endif
}

static int test_sem_holder_pool(unsigned long arg)
{
	(void)arg;

#if CONFIG_SEM_PREALLOCHOLDERS > 0
	struct tcb_s first;
	struct tcb_s second;
	irqstate_t flags;
	sem_t sem;

	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));

	if (sem_init(&sem, 0, 2) != OK) {
		dbg("sem_init for holder pool failed.\n");
		return ERROR;
	}

	flags = enter_critical_section();
	sem_addholder_tcb(&first, &sem);
	sem_addholder_tcb(&second, &sem);
	sem.semcount = 0;

	if (sem_findholder(&sem, &first) == NULL ||
		sem_findholder(&sem, &second) == NULL) {
		leave_critical_section(flags);
		dbg("preallocated holder pool did not track multiple tasks.\n");
		goto errout_with_sem_init;
	}

	/* A posting task or ISR need not be one of the holders.  Both anonymous
	 * releases must still consume one recorded permit.
	 */

	sem_releasecount(&sem, NULL);
	sem_releasecount(&sem, NULL);
	if (sem.semcount != 2 || sem_findholder(&sem, &first) != NULL ||
		sem_findholder(&sem, &second) != NULL) {
		leave_critical_section(flags);
		dbg("holder pool did not release both task counts.\n");
		goto errout_with_sem_init;
	}
	leave_critical_section(flags);

	if (sem_destroy(&sem) != OK) {
		dbg("sem_destroy after holder pool test failed.\n");
		return ERROR;
	}

	return OK;

errout_with_sem_init:
	sem.semcount = 0;
	sem_destroy(&sem);
	return ERROR;
#else
	return OK;
#endif
}

static int test_sem_waiter_handoff_case(unsigned long arg, bool multiple_counts)
{
#if CONFIG_SEM_PREALLOCHOLDERS > 0
	sem_t filler[CONFIG_SEM_PREALLOCHOLDERS];
	sem_t reserve;
	int filler_count = 0;
	bool reserve_held = false;
	bool reserve_initialized = false;
#endif
	irqstate_t flags;
	bool waiting = false;
	int count;
	int held_counts = 0;
	pid_t waiter;
	int wait_result;
#ifdef SAVE_SEM_HOLDER
	FAR struct semholder_s *holder;
	FAR struct tcb_s *self;
	FAR struct tcb_s *waiter_tcb;
	int releaser_holder_counts;
	int waiter_holder_counts;
#endif

	(void)arg;

	if (sem_init(&g_sem_waiter_sem, 0, multiple_counts ? 2 : 1) != OK) {
		dbg("sem_init for waiter handoff failed.\n");
		return ERROR;
	}

	if (sem_wait(&g_sem_waiter_sem) != OK) {
		dbg("initial sem_wait for waiter handoff failed.\n");
		goto errout_with_sem_init;
	}
	held_counts++;

	if (multiple_counts) {
		if (sem_wait(&g_sem_waiter_sem) != OK) {
			dbg("second sem_wait for waiter handoff failed.\n");
			goto errout_with_post;
		}
		held_counts++;
	}

#ifdef SAVE_SEM_HOLDER
	self = sched_self();
	if (sem_findholder(&g_sem_waiter_sem, self) == NULL ||
		sem_findholder(&g_sem_waiter_sem, self)->counts != held_counts) {
		dbg("initial waiter handoff count has no holder.\n");
		goto errout_with_post;
	}
#endif

#if CONFIG_SEM_PREALLOCHOLDERS > 0
	/* Reserve one holder before filling the pool for the multi-count case.
	 * Releasing it immediately before the handoff leaves exactly one slot
	 * for the waiter while the original owner retains its remaining count.
	 */

	if (multiple_counts) {
		if (sem_init(&reserve, 0, 1) != OK) {
			dbg("sem_init for holder reserve failed.\n");
			goto errout_with_post;
		}
		reserve_initialized = true;
		if (sem_wait(&reserve) != OK ||
			sem_findholder(&reserve, sched_self()) == NULL) {
			dbg("failed to reserve a semaphore holder pool slot.\n");
			goto errout_with_post;
		}
		reserve_held = true;
	}
#endif

	g_sem_waiter_context.sem = &g_sem_waiter_sem;
	g_sem_waiter_context.result = ERROR;
	g_sem_waiter_context.acquired = false;
	g_sem_waiter_context.release = false;
	g_sem_waiter_context.released = false;
	g_sem_waiter_context.terminate = false;
	waiter = kernel_thread("sem_waiter", SCHED_PRIORITY_DEFAULT, 2048,
						  test_sem_waiter, NULL);
	if (waiter <= 0) {
		dbg("kernel_thread for waiter handoff failed.\n");
		goto errout_with_post;
	}

	for (count = 0; count < 1000; count++) {
		flags = enter_critical_section();
		waiting = g_sem_waiter_sem.semcount < 0;
		leave_critical_section(flags);
		if (waiting) {
			break;
		}
		usleep(1000);
	}

#ifdef SAVE_SEM_HOLDER
	waiter_tcb = sched_gettcb(waiter);
	if (waiter_tcb == NULL) {
		dbg("failed to resolve waiter TCB.\n");
		goto errout_with_waiter;
	}
#endif

#if CONFIG_SEM_PREALLOCHOLDERS > 0
	/* Fill the remaining slots only after the waiter is blocked.  A
	 * single-count handoff must reuse the target's released holder.  A
	 * multi-count handoff releases the reserved slot below because the
	 * original holder must remain tracked.
	 */

	while (filler_count < CONFIG_SEM_PREALLOCHOLDERS -
		  (multiple_counts ? 2 : 1)) {
		if (sem_init(&filler[filler_count], 0, 1) != OK) {
			dbg("failed to fill the semaphore holder pool.\n");
			goto errout_with_waiter;
		}
		if (sem_wait(&filler[filler_count]) != OK) {
			sem_destroy(&filler[filler_count]);
			dbg("failed to acquire a semaphore holder pool slot.\n");
			goto errout_with_waiter;
		}
		if (sem_findholder(&filler[filler_count], sched_self()) == NULL) {
			sem_post(&filler[filler_count]);
			sem_destroy(&filler[filler_count]);
			break;
		}
		filler_count++;
	}

	if (reserve_held) {
		sem_post(&reserve);
		reserve_held = false;
	}
	if (reserve_initialized) {
		sem_destroy(&reserve);
		reserve_initialized = false;
	}
#endif

	if (sem_post(&g_sem_waiter_sem) != OK) {
		dbg("sem_post for waiter handoff failed.\n");
		goto errout_with_waiter;
	}
	held_counts--;

	for (count = 0; count < 1000 && !g_sem_waiter_context.acquired; count++) {
		usleep(1000);
	}

#ifdef SAVE_SEM_HOLDER
	flags = enter_critical_section();
	holder = sem_findholder(&g_sem_waiter_sem, waiter_tcb);
	waiter_holder_counts = holder != NULL ? holder->counts : 0;
	holder = sem_findholder(&g_sem_waiter_sem, self);
	releaser_holder_counts = holder != NULL ? holder->counts : 0;
	leave_critical_section(flags);
#endif

	g_sem_waiter_context.release = true;
	for (count = 0; count < 1000 && !g_sem_waiter_context.released; count++) {
		usleep(1000);
	}

#ifdef SAVE_SEM_HOLDER
	flags = enter_critical_section();
	holder = sem_findholder(&g_sem_waiter_sem, waiter_tcb);
	leave_critical_section(flags);
#endif

	g_sem_waiter_context.terminate = true;
	wait_result = test_sem_wait_child(waiter);
	if (wait_result != OK || !waiting || g_sem_waiter_context.result != OK ||
		!g_sem_waiter_context.acquired || !g_sem_waiter_context.released ||
		g_sem_waiter_sem.semcount != 1
#ifdef SAVE_SEM_HOLDER
		|| waiter_holder_counts != 1 ||
		releaser_holder_counts != (multiple_counts ? 1 : 0) ||
		holder != NULL
#endif
		) {
		dbg("blocked waiter handoff failed: multiple=%d wait=%d waiting=%d acquired=%d released=%d result=%d count=%d"
#ifdef SAVE_SEM_HOLDER
			" waiter_counts=%d releaser_counts=%d waiter_retained=%d"
#endif
			".\n",
			multiple_counts, wait_result, waiting, g_sem_waiter_context.acquired,
			g_sem_waiter_context.released, g_sem_waiter_context.result,
			g_sem_waiter_sem.semcount
#ifdef SAVE_SEM_HOLDER
			, waiter_holder_counts, releaser_holder_counts, holder != NULL
#endif
			);
		if (wait_result != OK) {
			test_sem_stop_child(waiter);
		}
		goto errout_with_sem_init;
	}

	while (held_counts > 0) {
		sem_post(&g_sem_waiter_sem);
		held_counts--;
	}

#ifdef SAVE_SEM_HOLDER
	flags = enter_critical_section();
	releaser_holder_counts =
		sem_findholder(&g_sem_waiter_sem, self) != NULL;
	leave_critical_section(flags);
	if (releaser_holder_counts != 0) {
		dbg("released waiter handoff retained the original holder.\n");
		goto errout_with_sem_init;
	}
#endif

	if (sem_destroy(&g_sem_waiter_sem) != OK) {
		dbg("sem_destroy after waiter handoff failed.\n");
		goto errout_with_fillers;
	}

#if CONFIG_SEM_PREALLOCHOLDERS > 0
	while (filler_count > 0) {
		filler_count--;
		sem_post(&filler[filler_count]);
		sem_destroy(&filler[filler_count]);
	}
#endif
	return OK;

errout_with_waiter:
	g_sem_waiter_context.release = true;
	g_sem_waiter_context.terminate = true;
	test_sem_stop_child(waiter);
errout_with_post:
	while (held_counts > 0) {
		sem_post(&g_sem_waiter_sem);
		held_counts--;
	}
errout_with_sem_init:
	sem_destroy(&g_sem_waiter_sem);
errout_with_fillers:
#if CONFIG_SEM_PREALLOCHOLDERS > 0
	if (reserve_held) {
		sem_post(&reserve);
	}
	if (reserve_initialized) {
		sem_destroy(&reserve);
	}
	while (filler_count > 0) {
		filler_count--;
		sem_post(&filler[filler_count]);
		sem_destroy(&filler[filler_count]);
	}
#endif
	return ERROR;
}

static int test_sem_waiter_handoff(unsigned long arg)
{
	if (test_sem_waiter_handoff_case(arg, false) != OK) {
		return ERROR;
	}

	return test_sem_waiter_handoff_case(arg, true);
}

static int test_sem_nonholder_post(unsigned long arg)
{
#ifdef SAVE_SEM_HOLDER
	FAR struct tcb_s *self;
#endif
	pid_t poster;
	int wait_result;

	(void)arg;

	if (sem_init(&g_sem_waiter_sem, 0, 1) != OK) {
		dbg("sem_init for non-holder post failed.\n");
		return ERROR;
	}

	if (sem_wait(&g_sem_waiter_sem) != OK) {
		dbg("sem_wait for non-holder post failed.\n");
		goto errout_with_sem_init;
	}

#ifdef SAVE_SEM_HOLDER
	self = sched_self();
#endif
	g_sem_waiter_context.sem = &g_sem_waiter_sem;
	g_sem_waiter_context.result = ERROR;
	poster = kernel_thread("sem_poster", SCHED_PRIORITY_DEFAULT, 2048,
						  test_sem_poster, NULL);
	if (poster <= 0) {
		dbg("kernel_thread for non-holder post failed.\n");
		goto errout_with_post;
	}

	wait_result = test_sem_wait_child(poster);
	if (wait_result != OK || g_sem_waiter_context.result != OK ||
		g_sem_waiter_sem.semcount != 1
#ifdef SAVE_SEM_HOLDER
		|| sem_findholder(&g_sem_waiter_sem, self) != NULL
#endif
		) {
		dbg("public non-holder sem_post did not release one permit.\n");
		if (wait_result != OK) {
			test_sem_stop_child(poster);
		}
		goto errout_with_sem_init;
	}

	if (sem_destroy(&g_sem_waiter_sem) != OK) {
		dbg("sem_destroy after non-holder post failed.\n");
		return ERROR;
	}

	return OK;

errout_with_post:
	sem_post(&g_sem_waiter_sem);
errout_with_sem_init:
	sem_destroy(&g_sem_waiter_sem);
	return ERROR;
}

static int test_sem_kernel(unsigned long arg)
{
	if (test_sem_reset(arg) != OK) {
		return ERROR;
	}

	if (test_sem_recover(arg) != OK) {
		return ERROR;
	}

	if (test_sem_protocol(arg) != OK) {
		return ERROR;
	}

	if (test_sem_holder(arg) != OK) {
		return ERROR;
	}

	if (test_sem_holder_multiple_counts(arg) != OK) {
		return ERROR;
	}

	if (test_sem_holder_pool(arg) != OK) {
		return ERROR;
	}

	if (test_sem_waiter_handoff(arg) != OK) {
		return ERROR;
	}

	if (test_sem_nonholder_post(arg) != OK) {
		return ERROR;
	}

	return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_sem(int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	switch (cmd) {
	case TESTIOC_SEM_TICK_WAIT_TEST:
		ret = test_sem_tick_wait(arg);
		break;
	case TESTIOC_SEM_KERNEL_TEST:
		ret = test_sem_kernel(arg);
		break;
	}
	return ret;
}
