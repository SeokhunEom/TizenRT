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
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#include <tinyara/irq.h>
#include <tinyara/os_api_test_drv.h>

#include "signal/signal.h"
#include "timer/timer.h"

/****************************************************************************
 * Private Function
 ****************************************************************************/

static int test_timer_count_allocated(void)
{
	FAR struct posix_timer_s *timer;
	FAR struct posix_timer_s *next;
	irqstate_t flags;
	int count = 0;

	flags = enter_critical_section();
	for (timer = (FAR struct posix_timer_s *)g_alloctimers.head; timer; timer = next) {
		next = timer->flink;
		count++;
	}

	leave_critical_section(flags);
	return count;
}

static int test_timer_count_owner(pid_t pid)
{
	FAR struct posix_timer_s *timer;
	FAR struct posix_timer_s *next;
	irqstate_t flags;
	int count = 0;

	flags = enter_critical_section();
	for (timer = (FAR struct posix_timer_s *)g_alloctimers.head; timer; timer = next) {
		next = timer->flink;
		if (timer->pt_owner == pid) {
			count++;
		}
	}

	leave_critical_section(flags);
	return count;
}

#if CONFIG_PREALLOC_TIMERS > 0
static int test_timer_count_free(void)
{
	FAR struct posix_timer_s *timer;
	FAR struct posix_timer_s *next;
	irqstate_t flags;
	int count = 0;

	flags = enter_critical_section();
	for (timer = (FAR struct posix_timer_s *)g_freetimers.head; timer; timer = next) {
		next = timer->flink;
		count++;
	}

	leave_critical_section(flags);
	return count;
}
#endif

static int test_timer_create_delete(unsigned long arg)
{
	int ret_chk;
	timer_t timer_id;
	timer_t gtimer_id;
	clockid_t clockid;
	struct sigevent st_sigevent;
	FAR struct posix_timer_s *timer;
	struct itimerspec timer_spec;

	(void)arg;

	st_sigevent.sigev_notify = SIGEV_SIGNAL;
	st_sigevent.sigev_signo = SIGRTMIN;
	st_sigevent.sigev_value.sival_ptr = &timer_id;

#ifdef CONFIG_CLOCK_MONOTONIC
	clockid = CLOCK_MONOTONIC;
	set_errno(0);
	ret_chk = timer_create(clockid, &st_sigevent, &timer_id);
	if (ret_chk != ERROR || get_errno() != EINVAL) {
		dbg("timer_create accepted unsupported clock id.\n");
		return ERROR;
	}
#endif

	clockid = CLOCK_REALTIME;
	set_errno(0);
	ret_chk = timer_create(clockid, &st_sigevent, NULL);
	if (ret_chk != ERROR || get_errno() != EINVAL) {
		dbg("timer_create accepted NULL timer id.\n");
		return ERROR;
	}

	set_errno(0);
	ret_chk = timer_delete(NULL);
	if (ret_chk != ERROR || get_errno() != EINVAL) {
		dbg("timer_delete accepted NULL timer id.\n");
		return ERROR;
	}

	ret_chk = timer_create(clockid, &st_sigevent, &timer_id);
	if (ret_chk == ERROR || timer_id == NULL) {
		dbg("timer_create failed.\n");
		return ERROR;
	}

	timer = (FAR struct posix_timer_s *)timer_id;
	if (timer->pt_value.sival_ptr != st_sigevent.sigev_value.sival_ptr ||
		timer->pt_signo != st_sigevent.sigev_signo ||
		timer->pt_crefs != 1 ||
		timer->pt_owner != getpid() ||
		timer->pt_delay != 0) {
		dbg("timer_create initialized unexpected timer fields.\n");
		goto errout_with_timer;
	}

	timer_spec.it_value.tv_sec = 1;
	timer_spec.it_value.tv_nsec = 0;
	timer_spec.it_interval.tv_sec = 1;
	timer_spec.it_interval.tv_nsec = 0;

	ret_chk = timer_settime(timer_id, 0, &timer_spec, NULL);
	if (ret_chk != OK) {
		dbg("timer_settime failed.\n");
		goto errout_with_timer;
	}

	ret_chk = timer_delete(timer_id);
	if (ret_chk == ERROR) {
		dbg("timer_delete failed.\n");
		goto errout_with_timer;
	}

	ret_chk = timer_create(CLOCK_REALTIME, NULL, &gtimer_id);
	if (ret_chk == ERROR || gtimer_id == NULL) {
		dbg("timer_create with default event failed.\n");
		return ERROR;
	}

	timer = (FAR struct posix_timer_s *)gtimer_id;
	if (timer->pt_value.sival_ptr != timer ||
		timer->pt_crefs != 1 ||
		timer->pt_owner != getpid() ||
		timer->pt_delay != 0) {
		dbg("timer_create initialized unexpected default event fields.\n");
		goto errout_with_gtimer;
	}

	ret_chk = timer_delete(gtimer_id);
	if (ret_chk == ERROR) {
		dbg("timer_delete failed.\n");
		goto errout_with_gtimer;
	}

	return OK;

errout_with_gtimer:
	timer_delete(gtimer_id);
	return ERROR;

errout_with_timer:
	timer_delete(timer_id);
	return ERROR;
}

static int test_timer_deleteall(unsigned long arg)
{
	(void)arg;

#if CONFIG_PREALLOC_TIMERS > 0
	int initalloc_cnt;
	int initfree_cnt;
	int createalloc_cnt;
	int createfree_cnt;
	int finalalloc_cnt;
	int finalfree_cnt;
	int initowner_cnt;
	timer_t timer_id1;
	timer_t timer_id2;
	pid_t owner;
	struct sigevent st_sigevent;

	st_sigevent.sigev_notify = SIGEV_SIGNAL;
	st_sigevent.sigev_signo = SIGRTMIN;
	st_sigevent.sigev_value.sival_ptr = &timer_id1;

	owner = getpid();
	initalloc_cnt = test_timer_count_allocated();
	initowner_cnt = test_timer_count_owner(owner);
	if (initowner_cnt != 0) {
		dbg("timer_deleteall test needs no pre-existing timers owned by this task.\n");
		return ERROR;
	}

	initfree_cnt = test_timer_count_free();
	if (initfree_cnt < 2) {
		dbg("timer_deleteall needs at least two free preallocated timers.\n");
		return ERROR;
	}

	if (timer_create(CLOCK_REALTIME, &st_sigevent, &timer_id1) == ERROR) {
		dbg("timer_create failed.\n");
		return ERROR;
	}

	st_sigevent.sigev_value.sival_ptr = &timer_id2;
	if (timer_create(CLOCK_REALTIME, &st_sigevent, &timer_id2) == ERROR) {
		dbg("timer_create failed.\n");
		timer_delete(timer_id1);
		return ERROR;
	}

	createalloc_cnt = test_timer_count_allocated();
	createfree_cnt = test_timer_count_free();
	if (createalloc_cnt != initalloc_cnt + 2 ||
		test_timer_count_owner(owner) != initowner_cnt + 2 ||
		createfree_cnt != initfree_cnt - 2) {
		dbg("timer_create did not update timer lists as expected.\n");
		goto errout_with_timers;
	}

	timer_deleteall(owner);

	finalalloc_cnt = test_timer_count_allocated();
	finalfree_cnt = test_timer_count_free();
	if (finalalloc_cnt != initalloc_cnt ||
		test_timer_count_owner(owner) != initowner_cnt ||
		finalfree_cnt != initfree_cnt) {
		dbg("timer_deleteall did not restore timer lists.\n");
		timer_deleteall(owner);
		return ERROR;
	}

	return OK;

errout_with_timers:
	timer_delete(timer_id1);
	timer_delete(timer_id2);
	return ERROR;
#else
	return OK;
#endif
}

static int test_timer_initialized_state(unsigned long arg)
{
	int ret_chk;
	timer_t timer_id;
	struct sigevent st_sigevent;
	FAR struct posix_timer_s *timer;
	int initalloc_cnt;
	int initowner_cnt;
	int createalloc_cnt;
	int createowner_cnt;
	int finalalloc_cnt;
	int finalowner_cnt;
#if CONFIG_PREALLOC_TIMERS > 0
	int initfree_cnt;
	int createfree_cnt;
	int finalfree_cnt;
#endif

	(void)arg;

	/* Set and enable alarm */
	st_sigevent.sigev_notify = SIGEV_SIGNAL;
	st_sigevent.sigev_signo = SIGRTMIN;
	st_sigevent.sigev_value.sival_ptr = &timer_id;

	initalloc_cnt = test_timer_count_allocated();
	initowner_cnt = test_timer_count_owner(getpid());

#if CONFIG_PREALLOC_TIMERS > 0
	initfree_cnt = test_timer_count_free();
	if (initfree_cnt < 0 || initfree_cnt > CONFIG_PREALLOC_TIMERS) {
		dbg("timer free list has invalid count.\n");
		return ERROR;
	}
#endif

	ret_chk = timer_create(CLOCK_REALTIME, &st_sigevent, &timer_id);
	if (ret_chk == ERROR || timer_id == NULL) {
		dbg("timer_create failed after timer_initialize.\n");
		return ERROR;
	}

	timer = (FAR struct posix_timer_s *)timer_id;
	if ((timer->pt_flags & PT_FLAGS_INUSE) == 0 ||
			timer->pt_owner != getpid()) {
		dbg("timer_create initialized invalid timer state.\n");
		goto errout_with_timer;
	}

	createalloc_cnt = test_timer_count_allocated();
	if (createalloc_cnt != initalloc_cnt + 1) {
		dbg("timer_create did not update allocated list.\n");
		goto errout_with_timer;
	}

	createowner_cnt = test_timer_count_owner(getpid());
	if (createowner_cnt != initowner_cnt + 1) {
		dbg("timer_create did not update owner count.\n");
		goto errout_with_timer;
	}

#if CONFIG_PREALLOC_TIMERS > 0
	createfree_cnt = test_timer_count_free();
	if ((timer->pt_flags & PT_FLAGS_PREALLOCATED) != 0) {
		if (createfree_cnt != initfree_cnt - 1) {
			dbg("timer_create did not consume a preallocated timer.\n");
			goto errout_with_timer;
		}
	} else if (createfree_cnt != initfree_cnt) {
		dbg("timer_create did not update free list.\n");
		goto errout_with_timer;
	}
#endif

	ret_chk = timer_delete(timer_id);
	if (ret_chk == ERROR) {
		dbg("timer_delete failed after timer_initialize.\n");
		goto errout_with_timer;
	}

	finalalloc_cnt = test_timer_count_allocated();
	if (finalalloc_cnt != initalloc_cnt) {
		dbg("timer_delete did not restore allocated list.\n");
		return ERROR;
	}

	finalowner_cnt = test_timer_count_owner(getpid());
	if (finalowner_cnt != initowner_cnt) {
		dbg("timer_delete did not restore owner count.\n");
		return ERROR;
	}

#if CONFIG_PREALLOC_TIMERS > 0
	finalfree_cnt = test_timer_count_free();
	if (finalfree_cnt != initfree_cnt) {
		dbg("timer_delete did not restore free list.\n");
		return ERROR;
	}
#endif

	return OK;

errout_with_timer:
	timer_delete(timer_id);
	return ERROR;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_timer(int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	switch (cmd) {
	case TESTIOC_TIMER_CREATE_DELETE_TEST:
		ret = test_timer_create_delete(arg);
		break;
	case TESTIOC_TIMER_DELETEALL_TEST:
		ret = test_timer_deleteall(arg);
		break;
	case TESTIOC_TIMER_INITIALIZE_TEST:
		ret = test_timer_initialized_state(arg);
		break;
	}
	return ret;
}
