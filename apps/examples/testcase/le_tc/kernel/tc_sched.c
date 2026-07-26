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

/// @file tc_sched.c

/// @brief Test Case Example for Sched API

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <tinyara/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <pthread.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <tinyara/sched.h>
#include <tinyara/os_api_test_drv.h>

#include "tc_internal.h"

#define SCHED_PRIORITY  13
#define LOOPCOUNT       2
#define ARRLEN          2
#define VAL_3           3
#define VAL_5           5
#define TASK_STACKSIZE  2048
#define INVALID_PID     -2
#define PID_IDLE        0
#define TASK_CANCEL_INVALID  -1

#if defined(CONFIG_BUILD_FLAT) && defined(CONFIG_SCHED_WAITPID) && \
	defined(CONFIG_SCHED_HAVE_PARENT) && defined(CONFIG_SCHED_CHILD_STATUS)
#define CHILD_RELEASE_RETRY            500
#define CHILD_RELEASE_USEC             1000
#define WAITID_EXIT_STATUS             23
#define WAITPID_ANY_EXIT_STATUS        37
#define REPARENT_CHILD_EXIT_STATUS     41
#define REPARENT_PARENT_EXIT_STATUS    43
#define REPARENT_RESULT_PENDING        1
#endif

pthread_t thread1, thread2;

pid_t g_task_pid;
bool g_callback = false;
bool g_pthread_callback = true;

/**
* @fn                   :sched_foreach_callback
* @description          :Function for tc_sched_sched_foreach
* @return               :void
*/
static void sched_foreach_callback(struct tcb_s *tcb, void *arg)
{
	/* it enumerate every task tcb, that means tcb created for current process will also be enumerated */
	if (tcb->pid == g_task_pid) {
		g_callback = true;
	}
}

#ifdef CONFIG_SCHED_WAITPID
static int sleep1sec_taskdel(int argc, char *argv[])
{
	sleep(1);
	task_delete(0);
	return 0;
}

#ifdef CONFIG_SCHED_HAVE_PARENT
static int sleep2sec_taskdel(int argc, char *argv[])
{
	sleep(2);
	task_delete(0);
	return 0;
}
#endif /* CONFIG_SCHED_HAVE_PARENT */

#if defined(CONFIG_BUILD_FLAT) && defined(CONFIG_SCHED_HAVE_PARENT) && \
	defined(CONFIG_SCHED_CHILD_STATUS)
static volatile pid_t g_reparent_child_pid;
static volatile int g_reparent_result;
static volatile bool g_reparent_start;
static volatile bool g_reparent_child_release;
static volatile bool g_reparent_parent_release;
static pid_t g_reparent_target_pid;

static int waitid_exited_child(int argc, char *argv[])
{
	return WAITID_EXIT_STATUS;
}

static int waitpid_any_exited_child(int argc, char *argv[])
{
	return WAITPID_ANY_EXIT_STATUS;
}

static int wait_for_task_release(pid_t pid)
{
	int retry;

	for (retry = 0; retry < CHILD_RELEASE_RETRY; retry++) {
		if (sched_gettcb(pid) == NULL) {
			return OK;
		}

		usleep(CHILD_RELEASE_USEC);
	}

	return ERROR;
}

static int wait_for_reparent_child_pid(void)
{
	int retry;

	for (retry = 0; retry < CHILD_RELEASE_RETRY; retry++) {
		if (g_reparent_child_pid > 0) {
			return OK;
		}

		usleep(CHILD_RELEASE_USEC);
	}

	return ERROR;
}

static int wait_for_reparent_result(void)
{
	int retry;

	for (retry = 0; retry < CHILD_RELEASE_RETRY; retry++) {
		if (g_reparent_result != REPARENT_RESULT_PENDING) {
			return OK;
		}

		usleep(CHILD_RELEASE_USEC);
	}

	return ERROR;
}

static int reparent_child_task(int argc, char *argv[])
{
	int retry;

	for (retry = 0; retry < CHILD_RELEASE_RETRY && !g_reparent_start; retry++) {
		usleep(CHILD_RELEASE_USEC);
	}

	if (!g_reparent_start) {
		return ERROR;
	}

	g_reparent_result = ioctl(tc_get_drvfd(), TESTIOC_TASK_REPARENT, g_reparent_target_pid);

	for (retry = 0; retry < CHILD_RELEASE_RETRY && !g_reparent_child_release; retry++) {
		usleep(CHILD_RELEASE_USEC);
	}

	if (!g_reparent_child_release) {
		return ERROR;
	}

	usleep(10 * CHILD_RELEASE_USEC);
	return REPARENT_CHILD_EXIT_STATUS;
}

static int reparent_parent_task(int argc, char *argv[])
{
	int retry;
	pid_t child_pid;

	child_pid = task_create("sched_reparent_child", SCHED_PRIORITY_DEFAULT,
			TASK_STACKSIZE, reparent_child_task, (char *const *)NULL);
	g_reparent_child_pid = child_pid;

	for (retry = 0; retry < CHILD_RELEASE_RETRY && !g_reparent_parent_release; retry++) {
		usleep(CHILD_RELEASE_USEC);
	}

	if (!g_reparent_parent_release) {
		return ERROR;
	}

	usleep(10 * CHILD_RELEASE_USEC);
	return REPARENT_PARENT_EXIT_STATUS;
}

static int set_unrelated_group_flag(uint8_t *saved_flags)
{
	struct tcb_s *rtcb = sched_self();

	if (rtcb == NULL || rtcb->group == NULL) {
		return ERROR;
	}

	*saved_flags = rtcb->group->tg_flags;
	rtcb->group->tg_flags = (*saved_flags & ~GROUP_FLAG_NOCLDWAIT) |
			GROUP_FLAG_PRIVILEGED;
	return OK;
}

static void restore_group_flags(uint8_t saved_flags)
{
	struct tcb_s *rtcb = sched_self();

	if (rtcb != NULL && rtcb->group != NULL) {
		rtcb->group->tg_flags = saved_flags;
	}
}
#endif
#endif /* CONFIG_SCHED_WAITPID */

/**
* @fn                   :threadfunc_callback
* @description          :Function for tc_sched_sched_yield
* @return               :void*
*/
static void *threadfunc_callback(void *param)
{
	g_pthread_callback = true;
	sleep(VAL_3);
	sched_yield();
	pthread_exit((pthread_addr_t)1);
	/* yield to another thread, g_pthread_callback will remain true in main process */
	g_pthread_callback = false;
	return NULL;
}

/**
* @fn                   :tc_sched_sched_setget_scheduler_param
* @brief                :set and get scheduler policies for the named process
* @scenario             :set and get scheduler policies for the named process, sched_getscheduler should return scheduler set
* API's covered         :sched_setscheduler, sched_getscheduler
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/
static void tc_sched_sched_setget_scheduler_param(void)
{
	int ret_chk = ERROR;
	struct sched_param st_setparam;
	struct sched_param st_getparam;
	struct sched_param prio_origin;
	int scheduler_origin;
	int loop_cnt = LOOPCOUNT;
	int arr_idx = 0;
	int sched_arr[ARRLEN] = { SCHED_RR, SCHED_FIFO };

	/* get original priority and scheduler of task */
	ret_chk = sched_getparam(getpid(), &prio_origin);
	TC_ASSERT_EQ("sched_getparam", ret_chk, OK);

	scheduler_origin = sched_getscheduler(getpid());
	TC_ASSERT_NEQ("sched_getscheduler", scheduler_origin, ERROR);

	/*  Check null priority parameter */

	ret_chk = sched_setparam(getpid(), NULL);
	TC_ASSERT_EQ_CLEANUP("sched_setparam", ret_chk, ERROR, sched_setparam(getpid(), &prio_origin));

	ret_chk = sched_getparam(getpid(), NULL);
	TC_ASSERT_EQ_CLEANUP("sched_getparam", ret_chk, ERROR, sched_setparam(getpid(), &prio_origin));

	/*  Check invalid priority parameter */

	st_setparam.sched_priority = SCHED_OTHER;
	ret_chk = sched_setscheduler(0, SCHED_OTHER, &st_setparam);
	TC_ASSERT_EQ_CLEANUP("sched_setscheduler", ret_chk, ERROR, sched_setparam(getpid(), &prio_origin));

	/*  Check for invalid scheduling policy */

	ret_chk = sched_setscheduler(0, SCHED_OTHER, &st_setparam);
	TC_ASSERT_EQ_CLEANUP("sched_setscheduler", ret_chk, ERROR, sched_setparam(getpid(), &prio_origin));
	TC_ASSERT_EQ("sched_setscheduler", errno, EINVAL);

	while (arr_idx < loop_cnt) {
		st_setparam.sched_priority = SCHED_PRIORITY;
		ret_chk = sched_setparam(getpid(), &st_setparam);
		TC_ASSERT_EQ_CLEANUP("sched_setparam", ret_chk, OK, sched_setparam(getpid(), &prio_origin));

		ret_chk = sched_setscheduler(getpid(), sched_arr[arr_idx], &st_setparam);
		TC_ASSERT_NEQ_CLEANUP("sched_setscheduler", ret_chk, ERROR, sched_setparam(getpid(), &prio_origin));

		/* ret_chk should be SCHED set */
		ret_chk = sched_getscheduler(getpid());
		TC_ASSERT_EQ_CLEANUP("sched_getscheduler", ret_chk, sched_arr[arr_idx], sched_setparam(getpid(), &prio_origin));

		ret_chk = sched_getparam(getpid(), &st_getparam);
		TC_ASSERT_EQ_CLEANUP("sched_getparam", ret_chk, OK, sched_setparam(getpid(), &prio_origin));
		TC_ASSERT_EQ_CLEANUP("sched_getparam", st_setparam.sched_priority, st_getparam.sched_priority, sched_setparam(getpid(), &prio_origin));
		arr_idx++;
	}

	/* restore the task priority and scheduler as previous after testing */
	ret_chk = sched_setscheduler(getpid(), scheduler_origin, &prio_origin);
	TC_ASSERT_NEQ("sched_setscheduler", ret_chk, ERROR);

	TC_SUCCESS_RESULT();
}

/**
* @fn                   :tc_sched_sched_rr_get_interval
* @brief                :get  the  SCHED_RR  interval for the named process
* @scenario             :get the SCHED_RR interval for the named process
* API's covered         :sched_rr_get_interval
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/
static void tc_sched_sched_rr_get_interval(void)
{
	int ret_chk;
	struct timespec st_timespec1;
	struct timespec st_timespec2;
	st_timespec1.tv_sec = 0;
	st_timespec1.tv_nsec = -1;

	st_timespec2.tv_sec = 0;
	st_timespec2.tv_nsec = -1;

	/* Check for invalid PID */

	ret_chk = sched_rr_get_interval(-1, &st_timespec1);
	TC_ASSERT_EQ("sched_rr_get_interval", ret_chk, ERROR);
	TC_ASSERT_EQ("sched_rr_get_interval", errno, EINVAL);

	/* Values are filled in st_timespec structure to differentiate them with values overwritten by rr_interval */

	ret_chk = sched_rr_get_interval(0, &st_timespec1);
	TC_ASSERT_NEQ("sched_rr_get_interval", ret_chk, ERROR);
	TC_ASSERT_GEQ("sched_rr_get_interval", st_timespec1.tv_nsec, 0);
	TC_ASSERT_LT("sched_rr_get_interval", st_timespec1.tv_nsec, 1000000000);

	ret_chk = sched_rr_get_interval(getpid(), &st_timespec2);
	TC_ASSERT_NEQ("sched_rr_get_interval", ret_chk, ERROR);
	TC_ASSERT_GEQ("sched_rr_get_interval", st_timespec2.tv_nsec, 0);
	TC_ASSERT_LT("sched_rr_get_interval", st_timespec2.tv_nsec, 1000000000);

	/* after sched_rr_get_interval() call, st_timespec structure should be overwritten with rr_interval values */

	TC_ASSERT_EQ("sched_rr_get_interval", st_timespec1.tv_sec, st_timespec2.tv_sec);
	TC_ASSERT_EQ("sched_rr_get_interval", st_timespec1.tv_nsec, st_timespec2.tv_nsec);

	/* Check for NULL interval */

	ret_chk = sched_rr_get_interval(getpid(), NULL);
	TC_ASSERT_EQ("sched_rr_get_interval", ret_chk, ERROR);
	TC_ASSERT_EQ("sched_rr_get_interval", errno, EFAULT);

	TC_SUCCESS_RESULT();
}

/**
* @fn                   :tc_sched_sched_yield
* @brief                :sched_yield() causes the calling thread to relinquish the CPU.
* @scenario             :sched_yield() causes the calling thread to relinquish the CPU.  The thread is moved
*                        to the end of the queue for its static priority and a new thread gets to run.
* API's covered         :sched_yield
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/
static void tc_sched_sched_yield(void)
{
	int ret_chk = 0;

	ret_chk = pthread_create(&thread1, NULL, threadfunc_callback, NULL);
	TC_ASSERT_EQ("pthread_create", ret_chk, OK);
	TC_ASSERT_EQ("sched_yield", g_pthread_callback, true);

	ret_chk = pthread_create(&thread2, NULL, threadfunc_callback, NULL);
	TC_ASSERT_EQ("pthread_create", ret_chk, OK);
	TC_ASSERT_EQ("sched_yield", g_pthread_callback, true);

	/* wait for threads to exit */
	pthread_join(thread1, 0);
	pthread_join(thread2, 0);
	TC_SUCCESS_RESULT();
}

/**
* @fn                   :tc_sched_wait
* @brief                :Suspends execution of the current process until one of its children terminates
* @scenario             :wait for state changes in a child of the calling process, and obtain information about
*                        the child whose state has changed. A state change is considered to be: the child
*                        terminated; the child was stopped by a signal; or the child was resumed by a signal
* API's covered         :wait
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/
#ifdef CONFIG_SCHED_WAITPID
#ifdef CONFIG_SCHED_HAVE_PARENT
static void tc_sched_wait(void)
{
	int ret_chk;
	pid_t child1_pid;
	pid_t child2_pid;
	int status;

	/* creating new process */
	child1_pid = task_create("sched1", SCHED_PRIORITY_DEFAULT, TASK_STACKSIZE, sleep1sec_taskdel, (char * const *)NULL);
	TC_ASSERT_GT("task_create", child1_pid, 0);

	child2_pid = task_create("sched2", SCHED_PRIORITY_DEFAULT, TASK_STACKSIZE, sleep2sec_taskdel, (char * const *)NULL);
	TC_ASSERT_GT("task_create", child2_pid, 0);

	/* child which exits first is handled by wait, here child1_pid exits earlier. */
	sleep(1);

	/* wait for child to exit, and store child's exit status */
	ret_chk = wait(&status);
	TC_ASSERT_NEQ("wait", ret_chk, ERROR);
	TC_ASSERT_EQ("wait", (child1_pid == (pid_t)ret_chk || child2_pid == (pid_t)ret_chk), true);

	/* wait for second child to exit */
	sleep(2);
	TC_SUCCESS_RESULT();
}

/**
* @fn                   :tc_sched_waitid
* @brief                :Suspends execution of the current process until one of its children changes state
* @scenario             :provides more precise control over which child state changes to wait for.
* API's covered         :waitid
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/

static void tc_sched_waitid(void)
{
	int ret_chk;
	pid_t child1_pid;
	pid_t child2_pid;
	siginfo_t info;
	int status;

	/* Check for The TCB corresponding to this PID is not our child. */

	ret_chk = waitid(P_PID, 0, &info, WEXITED);
	TC_ASSERT_EQ("waitid", ret_chk, ERROR);
	TC_ASSERT_EQ("waitid", errno, ECHILD);

	child1_pid = task_create("tc_waitid", SCHED_PRIORITY_DEFAULT, TASK_STACKSIZE, sleep1sec_taskdel, (char * const *)NULL);
	TC_ASSERT_GT("task_create", child1_pid, 0);

	/* Check for P_PID type */

	ret_chk = waitid(P_PID, child1_pid, &info, WEXITED);
	TC_ASSERT_NEQ("waitid", ret_chk, ERROR);
	TC_ASSERT_EQ("waitid", info.si_pid, child1_pid);

	/* Check for P_ALL ID type */

	child2_pid = task_create("tc_waitid", SCHED_PRIORITY_DEFAULT, TASK_STACKSIZE, sleep1sec_taskdel, (char * const *)NULL);
	TC_ASSERT_GT("task_create", child2_pid, 0);

	ret_chk = waitid(P_ALL, child2_pid, &info, WEXITED);
	TC_ASSERT_NEQ("waitid", ret_chk, ERROR);
	TC_ASSERT_EQ("waitid", (info.si_pid == child1_pid) || (info.si_pid == child2_pid), true);

	/* Check for other ID types that are not supported  */

	child1_pid = task_create("tc_waitid", SCHED_PRIORITY_DEFAULT, TASK_STACKSIZE, sleep1sec_taskdel, (char * const *)NULL);
	TC_ASSERT_GT("task_create", child1_pid, 0);

	ret_chk = waitid(P_GID, child1_pid, &info, WEXITED);
	TC_ASSERT_EQ("waitid", ret_chk, ERROR);
	TC_ASSERT_EQ("waitid", errno, ENOSYS);

	/* Check for options != WEXITED */
	ret_chk = waitid(P_PID, child1_pid, &info, 0);
	TC_ASSERT_EQ("waitid", ret_chk, ERROR);
	TC_ASSERT_EQ("waitid", errno, ENOSYS);

	ret_chk = waitpid(child1_pid, &status, 0);
	TC_ASSERT_EQ("waitpid", ret_chk, child1_pid);

	TC_SUCCESS_RESULT();
}
#endif

/**
* @fn                   :tc_sched_waitpid
* @brief                :Suspends the calling process until a specified process terminates
* @scenario             :The waitpid() system call suspends execution of the calling process until a child
*                        specified by pid argument has changed state. By default, waitpid() waits only for
*                        terminated children
* API's covered         :waitpid
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/

static void tc_sched_waitpid(void)
{
	int ret_chk;
	pid_t child_pid;
	int status;

	/* Check for The TCB corresponding to this PID is not our child. */
	ret_chk = waitpid(INVALID_PID, &status, 0);
	TC_ASSERT_EQ("waitpid", ret_chk, ERROR);
	TC_ASSERT_EQ("waitpid", errno, ECHILD);

	child_pid = task_create("tc_waitpid", SCHED_PRIORITY_DEFAULT, TASK_STACKSIZE, sleep2sec_taskdel, (char * const *)NULL);
	TC_ASSERT_GT("task_create", child_pid, 0);

	ret_chk = waitpid(child_pid, &status, 0);
	TC_ASSERT_EQ("waitpid", ret_chk, child_pid);

	/* None of the options are supported */

	ret_chk = waitpid(0, &status, 1);
	TC_ASSERT_EQ("waitpid", ret_chk, ERROR);
	TC_ASSERT_EQ("waitpid", errno, ENOSYS);

	TC_SUCCESS_RESULT();
}

#if defined(CONFIG_BUILD_FLAT) && defined(CONFIG_SCHED_HAVE_PARENT) && \
	defined(CONFIG_SCHED_CHILD_STATUS)
static void tc_sched_waitid_exited_child_before_wait(void)
{
	siginfo_t info;
	uint8_t saved_flags;
	pid_t child_pid;
	int ret_chk;

	ret_chk = set_unrelated_group_flag(&saved_flags);
	TC_ASSERT_EQ("set_unrelated_group_flag", ret_chk, OK);

	child_pid = task_create("waitid_exited", SCHED_PRIORITY_DEFAULT,
			TASK_STACKSIZE, waitid_exited_child, (char *const *)NULL);
	restore_group_flags(saved_flags);
	TC_ASSERT_GT("task_create", child_pid, 0);

	ret_chk = wait_for_task_release(child_pid);
	TC_ASSERT_EQ_CLEANUP("wait_for_task_release", ret_chk, OK, task_delete(child_pid));

	ret_chk = waitid(P_ALL, 0, &info, WEXITED);
	TC_ASSERT_EQ("waitid", ret_chk, OK);
	TC_ASSERT_EQ("waitid si_pid", info.si_pid, child_pid);
	TC_ASSERT_EQ("waitid si_code", info.si_code, CLD_EXITED);
	TC_ASSERT_EQ("waitid si_status", info.si_status, WAITID_EXIT_STATUS);

	TC_SUCCESS_RESULT();
}

static void tc_sched_waitpid_missed_signal(void)
{
	uint8_t saved_flags;
	pid_t child_pid;
	pid_t waited_pid;
	int ret_chk;
	int status;

	ret_chk = set_unrelated_group_flag(&saved_flags);
	TC_ASSERT_EQ("set_unrelated_group_flag", ret_chk, OK);

	child_pid = task_create("waitpid_missed", SCHED_PRIORITY_DEFAULT,
			TASK_STACKSIZE, waitpid_any_exited_child, (char *const *)NULL);
	restore_group_flags(saved_flags);
	TC_ASSERT_GT("task_create", child_pid, 0);

	ret_chk = wait_for_task_release(child_pid);
	TC_ASSERT_EQ_CLEANUP("wait_for_task_release", ret_chk, OK, task_delete(child_pid));

	waited_pid = waitpid((pid_t)-1, &status, 0);
	TC_ASSERT_EQ("waitpid(-1) child PID", waited_pid, child_pid);
	TC_ASSERT_EQ("waitpid(-1) WIFEXITED", WIFEXITED(status), true);
	TC_ASSERT_EQ("waitpid(-1) WEXITSTATUS", WEXITSTATUS(status), WAITPID_ANY_EXIT_STATUS);

	TC_SUCCESS_RESULT();
}

static void tc_sched_reparented_child(void)
{
	uint8_t saved_flags;
	pid_t parent_pid;
	pid_t child_pid;
	pid_t waited_pid;
	int ret_chk;
	int status;

	g_reparent_child_pid = INVALID_PID;
	g_reparent_result = REPARENT_RESULT_PENDING;
	g_reparent_start = false;
	g_reparent_child_release = false;
	g_reparent_parent_release = false;
	g_reparent_target_pid = getpid();

	parent_pid = task_create("sched_reparent_parent", SCHED_PRIORITY_DEFAULT,
			TASK_STACKSIZE, reparent_parent_task, (char *const *)NULL);
	TC_ASSERT_GT("task_create parent", parent_pid, 0);

	ret_chk = wait_for_reparent_child_pid();
	if (ret_chk != OK) {
		g_reparent_child_release = true;
		g_reparent_parent_release = true;
		task_delete(parent_pid);
		TC_ASSERT_EQ("reparent child PID", ret_chk, OK);
	}

	child_pid = g_reparent_child_pid;
	ret_chk = set_unrelated_group_flag(&saved_flags);
	if (ret_chk != OK) {
		g_reparent_child_release = true;
		g_reparent_parent_release = true;
		task_delete(child_pid);
		task_delete(parent_pid);
		TC_ASSERT_EQ("set_unrelated_group_flag", ret_chk, OK);
	}

	g_reparent_start = true;
	ret_chk = wait_for_reparent_result();
	restore_group_flags(saved_flags);
	if (ret_chk != OK || g_reparent_result != OK) {
		g_reparent_child_release = true;
		g_reparent_parent_release = true;
		usleep(20 * CHILD_RELEASE_USEC);
		task_delete(child_pid);
		task_delete(parent_pid);
		TC_ASSERT_EQ("task_reparent", g_reparent_result, OK);
	}

	g_reparent_child_release = true;
	waited_pid = waitpid(child_pid, &status, 0);
	if (waited_pid != child_pid) {
		g_reparent_parent_release = true;
		usleep(20 * CHILD_RELEASE_USEC);
		task_delete(parent_pid);
		TC_ASSERT_EQ("waitpid reparented child", waited_pid, child_pid);
	}

	TC_ASSERT_EQ_CLEANUP("reparented child WIFEXITED", WIFEXITED(status), true,
			g_reparent_parent_release = true);
	TC_ASSERT_EQ_CLEANUP("reparented child WEXITSTATUS", WEXITSTATUS(status),
			REPARENT_CHILD_EXIT_STATUS, g_reparent_parent_release = true);

	g_reparent_parent_release = true;
	waited_pid = waitpid(parent_pid, &status, 0);
	TC_ASSERT_EQ("waitpid original parent", waited_pid, parent_pid);
	TC_ASSERT_EQ("original parent WIFEXITED", WIFEXITED(status), true);
	TC_ASSERT_EQ("original parent WEXITSTATUS", WEXITSTATUS(status), REPARENT_PARENT_EXIT_STATUS);

	TC_SUCCESS_RESULT();
}
#endif
#endif

/**
* @fn                   :tc_sched_sched_lock_unlock
* @brief                :sched_lock disables context switching by disabling addition of new tasks,
*                        to the task list and increment lock count, sched_unlock decrements preemption lock count
* @scenario             :sched_lock increments lock count, sched_unlock decrements preemption lock count
* API's covered         :sched_lock, sched_unlock
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/

static void tc_sched_sched_lock_unlock(void)
{
	int ret_chk = ERROR;
	int cntlock;

	cntlock = sched_lockcount();

	ret_chk = sched_lock();
	TC_ASSERT_NEQ("sched_lock", ret_chk, ERROR);

	/* after sched_lock, lock count gets incremented */
	ret_chk = cntlock;
	cntlock = sched_lockcount();
	TC_ASSERT_EQ("sched_lock", ret_chk, cntlock - 1);

	ret_chk = sched_lock();
	TC_ASSERT_NEQ("sched_lock", ret_chk, ERROR);

	/* after sched_lock, lock count gets incremented */
	ret_chk = cntlock;
	cntlock = sched_lockcount();
	TC_ASSERT_EQ("sched_lock", ret_chk, cntlock - 1);

	ret_chk = sched_unlock();
	TC_ASSERT_NEQ("sched_unlock", ret_chk, ERROR);

	/* after sched_unlock, lock count gets decremented */
	ret_chk = cntlock;
	cntlock = sched_lockcount();
	TC_ASSERT_EQ("sched_unlock", ret_chk, cntlock + 1);

	ret_chk = sched_unlock();
	TC_ASSERT_NEQ("sched_unlock", ret_chk, ERROR);

	/* after sched_unlock, lock count gets decremented */
	ret_chk = cntlock;
	cntlock = sched_lockcount();
	TC_ASSERT_EQ("sched_unlock", ret_chk, cntlock + 1);

	TC_SUCCESS_RESULT();
}

/**
* @fn                   :pthread_sched_self
* @description          :Function for tc_sched_sched_self
* @return               :int
*/

static int pthread_sched_self(void *args)
{
	int fd;
	fd = tc_get_drvfd();

	return ioctl(fd, TESTIOC_GET_SELF_PID, 0);
}

/**
* @fn                   :tc_sched_sched_self
* @brief                :Return current thread tcb
* @scenario             :Return current thread tcb structure, verified by getting sched_gettcb(getpid)
* API's covered         :sched_self
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/

static void tc_sched_sched_self(void)
{
	int ret;
	int pid;
	pthread_t tid;
	ret = pthread_create(&tid, NULL, (pthread_startroutine_t)pthread_sched_self, NULL);
	TC_ASSERT_NEQ("pthread_create", ret, ERROR);

	/* Wait for the threads to stop */
	ret = pthread_join(tid, (void **)&pid);
	TC_ASSERT_NEQ("pthread_join", ret, ERROR);
	TC_ASSERT_EQ("sched_self", tid, pid);

	TC_SUCCESS_RESULT();
}

/**
* @fn                   :tc_sched_sched_foreach
* @brief                :Enumerate over each task and provide the TCB of each task to a user callback functions.
* @scenario             :provides TCB to user callback function "sched_foreach_callback"
* API's covered         :sched_foreach
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/
static void tc_sched_sched_foreach(void)
{
	g_callback = false;
	int fd;
	fd = tc_get_drvfd();
	g_task_pid = getpid();

	/* provides TCB to user callback function "sched_foreach_callback" */
	(void)ioctl(fd, TESTIOC_SCHED_FOREACH, (unsigned long)sched_foreach_callback);
	TC_ASSERT_EQ("sched_foreach", g_callback, true);

	TC_SUCCESS_RESULT();
}

/**
* @fn                   :tc_sched_sched_lockcount
* @brief                :sched_lockcount returns the lock count
* @scenario             :after sched_lock and sched_unlock, check the lockcount
* API's covered         :sched_lockcount
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/

static void tc_sched_sched_lockcount(void)
{
	int ret_chk = ERROR;
	int prev_cnt;
	int cur_cnt;

	prev_cnt = sched_lockcount();
	ret_chk = sched_lock();
	TC_ASSERT_NEQ("sched_lock", ret_chk, ERROR);

	/* after sched_lock, lock count gets incremented */
	cur_cnt = sched_lockcount();
	TC_ASSERT_EQ("sched_lockcount", prev_cnt, cur_cnt - 1);

	prev_cnt = cur_cnt;

	ret_chk = sched_unlock();
	TC_ASSERT_NEQ("sched_unlock", ret_chk, ERROR);

	/* after sched_unlock, lock count gets decremented */
	cur_cnt = sched_lockcount();
	TC_ASSERT_EQ("sched_lockcount", prev_cnt, cur_cnt + 1);

	TC_SUCCESS_RESULT();
}

#if CONFIG_NFILE_STREAMS > 0
/**
* @fn                   :tc_sched_sched_getstreams
* @brief                :return a pointer to the streams list for this thread
* @scenario             :check the streams list for current thread
* API's covered         :sched_getstreams
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/

static void tc_sched_sched_getstreams(void)
{
	struct streamlist *stream;

	stream = sched_getstreams();
	TC_ASSERT_NEQ("sched_getstreams", stream, NULL);

	TC_SUCCESS_RESULT();
}
#endif

#if !defined(CONFIG_BUILD_PROTECTED)
/**
 * @fn                   :tc_sched_task_setcancelstate
 * @brief                :This tc tests sched_task_setcancelstate()
 * @scenario             :If state is invalid, it will return ERROR and set errno to EINVAL
 *                        Else it will return OK and set canclestate to TASK_CANCEL_DISABLE or TASK_CANCEL_ENABLE
 * API's covered         :task_setcancelstate
 * Preconditions         :none
 * Postconditions        :none
 * @return               :void
 */

static void tc_sched_task_setcancelstate(void)
{
	int noncancelable_flag;
	int originstate;
	int oldstate;
	struct tcb_s *tcb = sched_self();
	int ret_chk;

	noncancelable_flag = tcb->flags & TCB_FLAG_NONCANCELABLE;
	originstate = noncancelable_flag == TCB_FLAG_NONCANCELABLE ? TASK_CANCEL_DISABLE : TASK_CANCEL_ENABLE;

	/* Negative case with invalid mode. It will return ERROR & set errno EINVAL */
	ret_chk = task_setcancelstate(TASK_CANCEL_INVALID, &oldstate);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", ret_chk, ERROR, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", get_errno(), EINVAL, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", oldstate, originstate, goto errout);

	/* Positive cases with valid mode. It will return OK */

	ret_chk = task_setcancelstate(TASK_CANCEL_DISABLE, &oldstate);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", ret_chk, OK, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", tcb->flags & TCB_FLAG_NONCANCELABLE, TCB_FLAG_NONCANCELABLE, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", oldstate, originstate, goto errout);

	ret_chk = task_setcancelstate(TASK_CANCEL_ENABLE, &oldstate);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", ret_chk, OK, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", tcb->flags & TCB_FLAG_NONCANCELABLE, 0, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", oldstate, TASK_CANCEL_DISABLE, goto errout);

	/* restore the cancestate */

	ret_chk = task_setcancelstate(originstate, NULL);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", ret_chk, OK, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcancelstate", tcb->flags & TCB_FLAG_NONCANCELABLE, noncancelable_flag, goto errout);

	TC_SUCCESS_RESULT();

errout:
	tcb->flags |= noncancelable_flag;
}

/**
 * @fn                   :tc_sched_task_setcanceltype
 * @brief                :This tc tests tc_sched_task_setcanceltype()
 * @Scenario             :The task_setcanceltype() function atomically both sets the calling
 *                        task's cancelability type to the indicated type and returns the
 *                        previous cancelability type at the location referenced by oldtype
 *                        If successful pthread_setcanceltype() function shall return zero;
 *                        otherwise, an error number shall be returned to indicate the error.
 * @API'scovered         :task_setcanceltype
 * @Preconditions        :none
 * @Postconditions       :none
 * @return               :void
 */
#ifdef CONFIG_CANCELLATION_POINTS
static void tc_sched_task_setcanceltype(void)
{
	int defferred_flag;
	int origintype;
	int oldtype;
	struct tcb_s *tcb = sched_self();
	int ret_chk;

	defferred_flag = tcb->flags & TCB_FLAG_CANCEL_DEFERRED;
	origintype = defferred_flag == TCB_FLAG_CANCEL_DEFERRED ? TASK_CANCEL_DEFERRED : TASK_CANCEL_ASYNCHRONOUS;

	/* Negative case with invalid mode. It will return EINVAL */
	ret_chk = task_setcanceltype(TASK_CANCEL_INVALID, &oldtype);
	TC_ASSERT_EQ_CLEANUP("task_setcanceltype", ret_chk, EINVAL, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcanceltype", oldtype, origintype, goto errout);

	/* Positive cases with valid mode. It will return OK */

	ret_chk = task_setcanceltype(TASK_CANCEL_DEFERRED, &oldtype);
	TC_ASSERT_EQ_CLEANUP("task_setcanceltype", ret_chk, OK, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcanceltype", tcb->flags & TCB_FLAG_CANCEL_DEFERRED, TCB_FLAG_CANCEL_DEFERRED, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcanceltype", oldtype, origintype, goto errout);

	ret_chk = task_setcanceltype(TASK_CANCEL_ASYNCHRONOUS, &oldtype);
	TC_ASSERT_EQ_CLEANUP("task_setcanceltype", ret_chk, OK, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcanceltype", tcb->flags & TCB_FLAG_CANCEL_DEFERRED, 0, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcanceltype", oldtype, TASK_CANCEL_DEFERRED, goto errout);

	/* restore the canceltype */

	ret_chk = task_setcanceltype(origintype, NULL);
	TC_ASSERT_EQ_CLEANUP("task_setcanceltype", ret_chk, OK, goto errout);
	TC_ASSERT_EQ_CLEANUP("task_setcanceltype", tcb->flags & TCB_FLAG_CANCEL_DEFERRED, defferred_flag, goto errout);

	TC_SUCCESS_RESULT();

errout:
	tcb->flags |= defferred_flag;
}
#endif
#endif


/**
* @fn                   :tc_sched_set_get_affinity
* @brief                :set and get cpu affinity for a task
* @scenario             :check if the cpu affinity is properly set
* API's covered         :sched_setaffinity, sched_getaffinity
* Preconditions         :none
* Postconditions        :none
* @return               :void
*/

static void tc_sched_set_get_affinity(void)
{
	cpu_set_t affinity = 1 << 0;	/* Set affinity to core zero */
#ifdef CONFIG_SMP
	TC_ASSERT_EQ("sched_setaffinity", sched_setaffinity(0, sizeof(cpu_set_t), &affinity), OK);
	affinity = 0;
	TC_ASSERT_EQ("sched_getaffinity", sched_getaffinity(0, sizeof(cpu_set_t), &affinity), OK);
	TC_ASSERT_EQ("sched_getaffinity", affinity, (1 << 0));
#else
	TC_ASSERT_EQ("sched_setaffinity", sched_setaffinity(0, sizeof(cpu_set_t), &affinity), -EINVAL);
	TC_ASSERT_EQ("sched_getaffinity", sched_getaffinity(0, sizeof(cpu_set_t), &affinity), 0);
#endif

	TC_SUCCESS_RESULT();
}

/****************************************************************************
 * Name: sched
 ****************************************************************************/
int sched_main(void)
{
#ifdef CONFIG_SCHED_WAITPID
#ifdef CONFIG_SCHED_HAVE_PARENT
	tc_sched_wait();
	tc_sched_waitid();
#endif
	tc_sched_waitpid();
#if defined(CONFIG_BUILD_FLAT) && defined(CONFIG_SCHED_HAVE_PARENT) && \
	defined(CONFIG_SCHED_CHILD_STATUS)
	tc_sched_waitid_exited_child_before_wait();
	tc_sched_waitpid_missed_signal();
	tc_sched_reparented_child();
#endif
#endif
	tc_sched_sched_setget_scheduler_param();
	tc_sched_sched_rr_get_interval();
	tc_sched_sched_yield();
	tc_sched_sched_lock_unlock();
	tc_sched_sched_self();
	tc_sched_sched_foreach();
	tc_sched_sched_lockcount();
#if CONFIG_NFILE_STREAMS > 0
	tc_sched_sched_getstreams();
#endif
#ifndef CONFIG_BUILD_PROTECTED
	tc_sched_task_setcancelstate();
#ifdef CONFIG_CANCELLATION_POINTS
	tc_sched_task_setcanceltype();
#endif
#endif
	tc_sched_set_get_affinity();

	return 0;
}
