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
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>

#include "signal/signal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TEST_SIGNAL_PENDING_SIGNO SIGUSR1
#define TEST_SIGNAL_MASK_SIGNO    SIGUSR2
#define TEST_SIGNAL_INVALID_HOW   0x5a

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile int g_test_signal_handler_count;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void test_signal_handler(int signo)
{
	if (signo == TEST_SIGNAL_PENDING_SIGNO) {
		g_test_signal_handler_count++;
	}
}

static int test_get_sig_findaction_add(unsigned long arg)
{
	FAR sigactq_t *sigact;
	sigact = sig_findaction(sched_self(), (int)arg);
	return (int)sigact;
}

static int test_sig_findaction_null(unsigned long arg)
{
	return sig_findaction(sched_self(), (int)arg) == NULL ? OK : ERROR;
}

static int test_signal_pause(unsigned long arg)
{
	int ret;

	(void)arg;

	ret = pause();              /* pause() always return -1 */
	if (ret == ERROR && get_errno() == EINTR) {
		return OK;
	}
	return ERROR;
}

static int test_get_tcb_sigprocmask(unsigned long arg)
{
	struct tcb_s *tcb;
	tcb = sched_gettcb((pid_t)arg);
	if (tcb == NULL) {
		dbg("sched_gettcb failed. errno : %d\n", get_errno());
		return ERROR;
	}
	return tcb->sigprocmask;
}

static int test_signal_mask_action_boundaries(void)
{
	struct sigaction oldact;
	sigset_t blockset;
	sigset_t savedset;
	sigset_t oldset;
	sigset_t currentset;
	int ret = ERROR;

	set_errno(0);
	if (sigaction(MAX_SIGNO + 1, NULL, &oldact) != ERROR || get_errno() != EINVAL) {
		dbg("sigaction accepted invalid signo.\n");
		return ERROR;
	}

	if (sig_findaction(NULL, TEST_SIGNAL_PENDING_SIGNO) != NULL) {
		dbg("sig_findaction accepted NULL tcb.\n");
		return ERROR;
	}

	if (sigprocmask(SIG_BLOCK, NULL, &savedset) != OK) {
		dbg("sigprocmask failed to read current mask.\n");
		return ERROR;
	}

	(void)sigemptyset(&blockset);
	(void)sigaddset(&blockset, TEST_SIGNAL_MASK_SIGNO);

	if (sigprocmask(TEST_SIGNAL_INVALID_HOW, &blockset, NULL) != ERROR) {
		dbg("sigprocmask accepted invalid how.\n");
		goto errout_restore_mask;
	}

	if (sigprocmask(SIG_BLOCK, NULL, &currentset) != OK || currentset != savedset) {
		dbg("invalid sigprocmask changed signal mask.\n");
		goto errout_restore_mask;
	}

	if (sigprocmask(SIG_BLOCK, &blockset, &oldset) != OK || oldset != savedset) {
		dbg("sigprocmask SIG_BLOCK failed.\n");
		goto errout_restore_mask;
	}

	if (sigprocmask(SIG_BLOCK, NULL, &currentset) != OK ||
		sigismember(&currentset, TEST_SIGNAL_MASK_SIGNO) != 1) {
		dbg("sigprocmask did not block expected signal.\n");
		goto errout_restore_mask;
	}

	ret = OK;

errout_restore_mask:
	if (sigprocmask(SIG_SETMASK, &savedset, NULL) != OK) {
		ret = ERROR;
	}

	return ret;
}

static int test_sig_pendingset(unsigned long arg)
{
	struct sigaction act;
	struct sigaction oldact;
	struct tcb_s *self;
	siginfo_t info;
	sigset_t blockset;
	sigset_t pendingset;
	sigset_t savedset;
	int ret = ERROR;

	(void)arg;
	self = sched_self();
	if (self == NULL) {
		return ERROR;
	}

	if (test_signal_mask_action_boundaries() != OK) {
		return ERROR;
	}

	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = test_signal_handler;
	(void)sigemptyset(&act.sa_mask);

	g_test_signal_handler_count = 0;
	if (sigaction(TEST_SIGNAL_PENDING_SIGNO, &act, &oldact) != OK) {
		return ERROR;
	}

	(void)sigemptyset(&blockset);
	(void)sigaddset(&blockset, TEST_SIGNAL_PENDING_SIGNO);
	if (sigprocmask(SIG_BLOCK, &blockset, &savedset) != OK) {
		goto errout_with_action;
	}

	if (kill(getpid(), TEST_SIGNAL_PENDING_SIGNO) == ERROR) {
		goto errout_with_mask;
	}

	pendingset = sig_pendingset(self);
	if (sigismember(&pendingset, TEST_SIGNAL_PENDING_SIGNO) != 1) {
		goto errout_with_mask;
	}

	memset(&info, 0, sizeof(siginfo_t));
	if (sigwaitinfo(&blockset, &info) != TEST_SIGNAL_PENDING_SIGNO || info.si_signo != TEST_SIGNAL_PENDING_SIGNO) {
		goto errout_with_mask;
	}

	pendingset = sig_pendingset(self);
	if (sigismember(&pendingset, TEST_SIGNAL_PENDING_SIGNO) == 1) {
		goto errout_with_mask;
	}

	if (g_test_signal_handler_count == 0) {
		ret = OK;
	}

errout_with_mask:
	if (sigprocmask(SIG_SETMASK, &savedset, NULL) != OK) {
		ret = ERROR;
	}

errout_with_action:
	if (sigaction(TEST_SIGNAL_PENDING_SIGNO, &oldact, NULL) != OK) {
		ret = ERROR;
	}

	return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_signal(int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	switch (cmd) {
	case TESTIOC_GET_SIG_FINDACTION_ADD:
		ret = test_get_sig_findaction_add(arg);
		break;
	case TESTIOC_SIG_FINDACTION_NULL_TEST:
		ret = test_sig_findaction_null(arg);
		break;
	case TESTIOC_SIGNAL_PAUSE:
		ret = test_signal_pause(arg);
		break;
	case TESTIOC_GET_TCB_SIGPROCMASK:
		ret = test_get_tcb_sigprocmask(arg);
		break;
	case TESTIOC_SIG_PENDINGSET_TEST:
		ret = test_sig_pendingset(arg);
		break;
	}
	return ret;
}
