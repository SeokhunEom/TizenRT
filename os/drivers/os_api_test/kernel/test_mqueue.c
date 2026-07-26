/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
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
#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <tinyara/irq.h>
#include <tinyara/mqueue.h>
#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>

#include "mqueue/mqueue.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_MQUEUE_MSGSIZE	16
#define TEST_MQUEUE_MAXMSG	3
#define TEST_MQUEUE_LOW_PRIO	1
#define TEST_MQUEUE_HIGH_PRIO	10
#define TEST_MQUEUE_LOW_MSG	"low"
#define TEST_MQUEUE_HIGH_MSG	"high"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_mqueue_count(FAR sq_queue_t *queue)
{
	FAR sq_entry_t *entry;
	int count = 0;

	for (entry = queue->head; entry; entry = entry->flink) {
		count++;
	}

	return count;
}

static bool test_mqueue_has_descriptor(FAR struct task_group_s *group, mqd_t mqdes)
{
	FAR struct mq_des *curr;

	for (curr = (FAR struct mq_des *)group->tg_msgdesq.head; curr; curr = curr->flink) {
		if (curr == mqdes) {
			return true;
		}
	}

	return false;
}

static FAR struct mqueue_inode_s *test_mqueue_alloc_msgq(void)
{
	struct mq_attr attr;
	FAR struct mqueue_inode_s *msgq;

	attr.mq_flags = 0;
	attr.mq_maxmsg = TEST_MQUEUE_MAXMSG;
	attr.mq_msgsize = TEST_MQUEUE_MSGSIZE;
	attr.mq_curmsgs = 0;

	msgq = mq_msgqalloc(0666, &attr);
	if (msgq == NULL) {
		dbg("mq_msgqalloc failed.\n");
		return NULL;
	}

	if (msgq->maxmsgs != TEST_MQUEUE_MAXMSG ||
		msgq->maxmsgsize != TEST_MQUEUE_MSGSIZE ||
		msgq->nmsgs != 0 ||
		msgq->nwaitnotfull != 0 ||
		msgq->nwaitnotempty != 0 ||
		msgq->msglist.head != NULL) {
		dbg("mq_msgqalloc initialized unexpected fields.\n");
		mq_msgqfree(msgq);
		return NULL;
	}

#ifndef CONFIG_DISABLE_SIGNALS
	if (msgq->ntpid != INVALID_PROCESS_ID || msgq->ntmqdes != NULL) {
		dbg("mq_msgqalloc initialized unexpected notification fields.\n");
		mq_msgqfree(msgq);
		return NULL;
	}
#endif

	return msgq;
}

static void test_mqueue_desclose(mqd_t mqdes, FAR struct task_group_s *group)
{
	sched_lock();
	mq_desclose_group(mqdes, group);
	sched_unlock();
}

static FAR struct mqueue_msg_s *test_mqueue_waitreceive(mqd_t mqdes)
{
	FAR struct mqueue_msg_s *mqmsg;
	irqstate_t flags;

	flags = enter_critical_section();
	mqmsg = mq_waitreceive(mqdes);
	leave_critical_section(flags);

	return mqmsg;
}

static int test_mqueue_descriptor(unsigned long arg)
{
	FAR struct tcb_s *self;
	FAR struct task_group_s *group;
	FAR struct mqueue_inode_s *msgq;
	mqd_t mqdes;
	int init_group_count;

	(void)arg;

	self = sched_self();
	if (self == NULL || self->group == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	group = self->group;
	init_group_count = test_mqueue_count(&group->tg_msgdesq);

	msgq = test_mqueue_alloc_msgq();
	if (msgq == NULL) {
		return ERROR;
	}

	mqdes = mq_descreate(NULL, msgq, O_RDWR | O_NONBLOCK);
	if (mqdes == NULL) {
		dbg("mq_descreate failed.\n");
		mq_msgqfree(msgq);
		return ERROR;
	}

	if (mqdes->msgq != msgq ||
		mqdes->oflags != (O_RDWR | O_NONBLOCK) ||
		test_mqueue_count(&group->tg_msgdesq) != init_group_count + 1 ||
		!test_mqueue_has_descriptor(group, mqdes)) {
		dbg("mq_descreate initialized unexpected descriptor state.\n");
		goto errout_with_descriptor;
	}

#ifndef CONFIG_DISABLE_SIGNALS
	msgq->ntmqdes = mqdes;
	msgq->ntpid = getpid();
	msgq->ntsigno = SIGUSR1;
	msgq->ntvalue.sival_int = TEST_MQUEUE_HIGH_PRIO;
#endif

	test_mqueue_desclose(mqdes, group);
	if (test_mqueue_count(&group->tg_msgdesq) != init_group_count ||
		test_mqueue_has_descriptor(group, mqdes)) {
		dbg("mq_desclose_group did not remove descriptor.\n");
		mq_msgqfree(msgq);
		return ERROR;
	}

#ifndef CONFIG_DISABLE_SIGNALS
	if (msgq->ntmqdes != NULL ||
		msgq->ntpid != INVALID_PROCESS_ID ||
		msgq->ntsigno != 0 ||
		msgq->ntvalue.sival_int != 0) {
		dbg("mq_desclose_group did not detach notification.\n");
		mq_msgqfree(msgq);
		return ERROR;
	}
#endif

	mq_msgqfree(msgq);
	return OK;

errout_with_descriptor:
	test_mqueue_desclose(mqdes, group);
	mq_msgqfree(msgq);
	return ERROR;
}

static int test_mqueue_verify(mqd_t mqdes)
{
	char buffer[TEST_MQUEUE_MSGSIZE];

	set_errno(0);
	if (mq_verifysend(NULL, TEST_MQUEUE_LOW_MSG, sizeof(TEST_MQUEUE_LOW_MSG),
			TEST_MQUEUE_LOW_PRIO) != ERROR || get_errno() != EINVAL) {
		dbg("mq_verifysend accepted NULL descriptor.\n");
		return ERROR;
	}

	set_errno(0);
	if (mq_verifysend(mqdes, NULL, sizeof(TEST_MQUEUE_LOW_MSG), TEST_MQUEUE_LOW_PRIO) != ERROR || get_errno() != EINVAL) {
		dbg("mq_verifysend accepted NULL message.\n");
		return ERROR;
	}

	set_errno(0);
	if (mq_verifysend(mqdes, TEST_MQUEUE_LOW_MSG, TEST_MQUEUE_MSGSIZE + 1,
			TEST_MQUEUE_LOW_PRIO) != ERROR || get_errno() != EMSGSIZE) {
		dbg("mq_verifysend accepted oversized message.\n");
		return ERROR;
	}

	set_errno(0);
	if (mq_verifyreceive(NULL, buffer, sizeof(buffer)) != ERROR || get_errno() != EINVAL) {
		dbg("mq_verifyreceive accepted NULL descriptor.\n");
		return ERROR;
	}

	set_errno(0);
	if (mq_verifyreceive(mqdes, NULL, sizeof(buffer)) != ERROR || get_errno() != EINVAL) {
		dbg("mq_verifyreceive accepted NULL buffer.\n");
		return ERROR;
	}

	set_errno(0);
	if (mq_verifyreceive(mqdes, buffer, TEST_MQUEUE_MSGSIZE - 1) != ERROR || get_errno() != EMSGSIZE) {
		dbg("mq_verifyreceive accepted undersized buffer.\n");
		return ERROR;
	}

	if (mq_verifysend(mqdes, TEST_MQUEUE_LOW_MSG, sizeof(TEST_MQUEUE_LOW_MSG), TEST_MQUEUE_LOW_PRIO) != OK ||
		mq_verifyreceive(mqdes, buffer, sizeof(buffer)) != OK) {
		dbg("mqueue verify accepted path failed.\n");
		return ERROR;
	}

	return OK;
}

static int test_mqueue_attr(unsigned long arg)
{
	FAR struct tcb_s *self;
	FAR struct task_group_s *group;
	FAR struct mqueue_inode_s *msgq;
	struct mq_attr attr;
	struct mq_attr newattr;
	struct mq_attr oldattr;
	char buffer[TEST_MQUEUE_MSGSIZE];
	mqd_t mqdes;

	(void)arg;

	self = sched_self();
	if (self == NULL || self->group == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	group = self->group;
	msgq = test_mqueue_alloc_msgq();
	if (msgq == NULL) {
		return ERROR;
	}

	mqdes = mq_descreate(NULL, msgq, O_RDWR);
	if (mqdes == NULL) {
		dbg("mq_descreate failed.\n");
		mq_msgqfree(msgq);
		return ERROR;
	}

	if (mq_getattr(NULL, &attr) != ERROR ||
		mq_getattr(mqdes, NULL) != ERROR) {
		dbg("mq_getattr accepted invalid arguments.\n");
		goto errout_with_descriptor;
	}

	if (mq_getattr(mqdes, &attr) != OK ||
		attr.mq_flags != O_RDWR ||
		attr.mq_maxmsg != TEST_MQUEUE_MAXMSG ||
		attr.mq_msgsize != TEST_MQUEUE_MSGSIZE ||
		attr.mq_curmsgs != 0) {
		dbg("mq_getattr returned unexpected attributes.\n");
		goto errout_with_descriptor;
	}

	memset(&newattr, 0, sizeof(newattr));
	newattr.mq_flags = O_NONBLOCK | O_CREAT;

	if (mq_setattr(NULL, &newattr, &oldattr) != ERROR ||
		mq_setattr(mqdes, NULL, &oldattr) != ERROR) {
		dbg("mq_setattr accepted invalid arguments.\n");
		goto errout_with_descriptor;
	}

	memset(&oldattr, 0, sizeof(oldattr));
	if (mq_setattr(mqdes, &newattr, &oldattr) != OK ||
		oldattr.mq_flags != O_RDWR ||
		oldattr.mq_maxmsg != TEST_MQUEUE_MAXMSG ||
		oldattr.mq_msgsize != TEST_MQUEUE_MSGSIZE ||
		oldattr.mq_curmsgs != 0 ||
		mqdes->oflags != (O_RDWR | O_NONBLOCK)) {
		dbg("mq_setattr did not update O_NONBLOCK as expected.\n");
		goto errout_with_descriptor;
	}

	if (mq_getattr(mqdes, &attr) != OK ||
		attr.mq_flags != (O_RDWR | O_NONBLOCK) ||
		attr.mq_maxmsg != TEST_MQUEUE_MAXMSG ||
		attr.mq_msgsize != TEST_MQUEUE_MSGSIZE ||
		attr.mq_curmsgs != 0) {
		dbg("mq_getattr did not reflect updated flags.\n");
		goto errout_with_descriptor;
	}

	set_errno(0);
	if (mq_receive(mqdes, buffer, sizeof(buffer), NULL) != ERROR ||
		get_errno() != EAGAIN) {
		dbg("mq_receive did not honor O_NONBLOCK on empty queue.\n");
		goto errout_with_descriptor;
	}

	newattr.mq_flags = 0;
	if (mq_setattr(mqdes, &newattr, NULL) != OK ||
		mqdes->oflags != O_RDWR) {
		dbg("mq_setattr did not clear O_NONBLOCK.\n");
		goto errout_with_descriptor;
	}

	test_mqueue_desclose(mqdes, group);
	mq_msgqfree(msgq);
	return OK;

errout_with_descriptor:
	test_mqueue_desclose(mqdes, group);
	mq_msgqfree(msgq);
	return ERROR;
}

static int test_mqueue_send_receive(unsigned long arg)
{
	FAR struct tcb_s *self;
	FAR struct task_group_s *group;
	FAR struct mqueue_inode_s *msgq;
	FAR struct mqueue_msg_s *mqmsg;
	FAR struct mqueue_msg_s *rcvmsg;
	char buffer[TEST_MQUEUE_MSGSIZE];
	mqd_t mqdes;
	int prio;

	(void)arg;

	self = sched_self();
	if (self == NULL || self->group == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	group = self->group;
	msgq = test_mqueue_alloc_msgq();
	if (msgq == NULL) {
		return ERROR;
	}

	mqdes = mq_descreate(NULL, msgq, O_RDWR | O_NONBLOCK);
	if (mqdes == NULL) {
		dbg("mq_descreate failed.\n");
		mq_msgqfree(msgq);
		return ERROR;
	}

	if (test_mqueue_verify(mqdes) != OK) {
		goto errout_with_descriptor;
	}

	mqmsg = mq_msgalloc();
	if (mqmsg == NULL) {
		dbg("mq_msgalloc failed.\n");
		goto errout_with_descriptor;
	}

	if (mq_dosend(mqdes, mqmsg, TEST_MQUEUE_LOW_MSG, sizeof(TEST_MQUEUE_LOW_MSG), TEST_MQUEUE_LOW_PRIO) != OK) {
		dbg("mq_dosend failed.\n");
		mq_msgfree(mqmsg);
		goto errout_with_descriptor;
	}

	mqmsg = mq_msgalloc();
	if (mqmsg == NULL) {
		dbg("mq_msgalloc failed.\n");
		goto errout_with_descriptor;
	}

	if (mq_dosend(mqdes, mqmsg, TEST_MQUEUE_HIGH_MSG, sizeof(TEST_MQUEUE_HIGH_MSG), TEST_MQUEUE_HIGH_PRIO) != OK) {
		dbg("mq_dosend failed.\n");
		mq_msgfree(mqmsg);
		goto errout_with_descriptor;
	}

	if (msgq->nmsgs != 2 ||
		((FAR struct mqueue_msg_s *)msgq->msglist.head)->priority != TEST_MQUEUE_HIGH_PRIO) {
		dbg("mq_dosend did not maintain priority order.\n");
		goto errout_with_descriptor;
	}

	rcvmsg = test_mqueue_waitreceive(mqdes);
	if (rcvmsg == NULL) {
		dbg("mq_waitreceive failed.\n");
		goto errout_with_descriptor;
	}

	memset(buffer, 0, sizeof(buffer));
	prio = 0;
	if (mq_doreceive(mqdes, rcvmsg, buffer, &prio) != sizeof(TEST_MQUEUE_HIGH_MSG) ||
		prio != TEST_MQUEUE_HIGH_PRIO ||
		strcmp(buffer, TEST_MQUEUE_HIGH_MSG) != 0 ||
		msgq->nmsgs != 1) {
		dbg("mq_doreceive returned unexpected high priority message.\n");
		goto errout_with_descriptor;
	}

	rcvmsg = test_mqueue_waitreceive(mqdes);
	if (rcvmsg == NULL) {
		dbg("mq_waitreceive failed.\n");
		goto errout_with_descriptor;
	}

	memset(buffer, 0, sizeof(buffer));
	prio = 0;
	if (mq_doreceive(mqdes, rcvmsg, buffer, &prio) != sizeof(TEST_MQUEUE_LOW_MSG) ||
		prio != TEST_MQUEUE_LOW_PRIO ||
		strcmp(buffer, TEST_MQUEUE_LOW_MSG) != 0 ||
		msgq->nmsgs != 0 ||
		msgq->msglist.head != NULL) {
		dbg("mq_doreceive returned unexpected low priority message.\n");
		goto errout_with_descriptor;
	}

	test_mqueue_desclose(mqdes, group);
	mq_msgqfree(msgq);
	return OK;

errout_with_descriptor:
	test_mqueue_desclose(mqdes, group);
	mq_msgqfree(msgq);
	return ERROR;
}

static int test_mqueue_recover(unsigned long arg)
{
	FAR struct mqueue_inode_s *msgq;
	struct tcb_s tcb;

	(void)arg;

	msgq = test_mqueue_alloc_msgq();
	if (msgq == NULL) {
		return ERROR;
	}

	memset(&tcb, 0, sizeof(tcb));
	msgq->nwaitnotempty = 2;
	tcb.task_state = TSTATE_WAIT_MQNOTEMPTY;
	tcb.msgwaitq = msgq;
	mq_recover(&tcb);
	if (msgq->nwaitnotempty != 1) {
		dbg("mq_recover did not decrement not-empty waiters.\n");
		mq_msgqfree(msgq);
		return ERROR;
	}

	memset(&tcb, 0, sizeof(tcb));
	msgq->nwaitnotfull = 2;
	tcb.task_state = TSTATE_WAIT_MQNOTFULL;
	tcb.msgwaitq = msgq;
	mq_recover(&tcb);
	if (msgq->nwaitnotfull != 1) {
		dbg("mq_recover did not decrement not-full waiters.\n");
		mq_msgqfree(msgq);
		return ERROR;
	}

	memset(&tcb, 0, sizeof(tcb));
	tcb.task_state = TSTATE_TASK_RUNNING;
	tcb.msgwaitq = msgq;
	mq_recover(&tcb);
	if (msgq->nwaitnotempty != 1 || msgq->nwaitnotfull != 1) {
		dbg("mq_recover changed counters for non-mqueue wait state.\n");
		mq_msgqfree(msgq);
		return ERROR;
	}

	mq_msgqfree(msgq);
	return OK;
}

static int test_mqueue_all(unsigned long arg)
{
	if (test_mqueue_descriptor(arg) != OK) {
		return ERROR;
	}

	if (test_mqueue_attr(arg) != OK) {
		return ERROR;
	}

	if (test_mqueue_send_receive(arg) != OK) {
		return ERROR;
	}

	if (test_mqueue_recover(arg) != OK) {
		return ERROR;
	}

	return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_mqueue(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_MQUEUE_TEST:
		ret = test_mqueue_all(arg);
		break;
	}

	return ret;
}
