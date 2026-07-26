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
#include <time.h>
#include <semaphore.h>
#include <string.h>

#include <tinyara/semaphore.h>
#include <tinyara/sched.h>
#include <tinyara/os_api_test_drv.h>

#include "clock/clock.h"
#include "semaphore/semaphore.h"

/****************************************************************************
 * Private Function
 ****************************************************************************/

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

	return OK;

errout_with_sem_init:
	sem.semcount = 0;
	sem_destroy(&sem);
	return ERROR;
}

static int test_sem_protocol(unsigned long arg)
{
	(void)arg;

#ifdef CONFIG_PRIORITY_INHERITANCE
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

	if (sem_setprotocol(&sem, SEM_PRIO_NONE) != OK ||
		(sem.flags & PRIOINHERIT_FLAGS_DISABLE) == 0) {
		dbg("sem_setprotocol failed to disable priority inheritance.\n");
		goto errout_with_sem_init;
	}

	if (sem_setprotocol(&sem, SEM_PRIO_INHERIT) != OK ||
		(sem.flags & PRIOINHERIT_FLAGS_DISABLE) != 0) {
		dbg("sem_setprotocol failed to enable priority inheritance.\n");
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
#else
	return OK;
#endif
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
