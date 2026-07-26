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
#include <sched.h>
#include <stdbool.h>
#include <string.h>

#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>

#include "sched/sched.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TASK_CANCEL_INVALID	(-1)
#define TEST_INVALID_PID	(-1)
#define TEST_SCHED_FOREACH_COOKIE	0x53434844

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct tcb_s *tcb;

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct test_sched_foreach_s {
	unsigned int cookie;
	FAR struct tcb_s *self;
	pid_t worker_pid;
	int count;
	int found_self;
	int found_worker;
	int bad_tcb;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_get_self_pid(unsigned long arg)
{
	(void)arg;

	tcb = sched_self();
	if (tcb == NULL) {
		return ERROR;
	}
	return tcb->pid;
}

static int test_is_alive_thread(unsigned long arg)
{
	tcb = sched_gettcb((pid_t)arg);
	if (tcb == NULL) {
		dbg("sched_gettcb failed. errno : %d\n", get_errno());
		return ERROR;
	}
	return OK;
}

static int test_get_tcb_adj_stack_size(unsigned long arg)
{
	tcb = sched_gettcb((pid_t)arg);
	if (tcb == NULL) {
		dbg("sched_gettcb failed. errno : %d\n", get_errno());
		return ERROR;
	}
	return tcb->adj_stack_size;
}

static void test_sched_foreach_callback(FAR struct tcb_s *cur_tcb, FAR void *arg)
{
	FAR struct test_sched_foreach_s *ctx = (FAR struct test_sched_foreach_s *)arg;

	if (ctx == NULL || ctx->cookie != TEST_SCHED_FOREACH_COOKIE) {
		return;
	}

	if (cur_tcb == NULL || sched_gettcb(cur_tcb->pid) != cur_tcb ||
		sched_verifytcb(cur_tcb) != true) {
		ctx->bad_tcb++;
		return;
	}

	ctx->count++;
	if (cur_tcb == ctx->self && cur_tcb->pid == ctx->self->pid) {
		ctx->found_self++;
	}

	if (ctx->worker_pid >= 0 && cur_tcb->pid == ctx->worker_pid) {
		ctx->found_worker++;
	}
}

static int test_sched_foreach(unsigned long arg)
{
	sched_foreach((sched_foreach_t)arg, NULL);

	return OK;
}

static int test_sched_foreach_verify(FAR struct tcb_s *self, pid_t worker_pid)
{
	struct test_sched_foreach_s ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.cookie = TEST_SCHED_FOREACH_COOKIE;
	ctx.self = self;
	ctx.worker_pid = worker_pid;

	sched_foreach(test_sched_foreach_callback, &ctx);
	if (ctx.count <= 0 || ctx.count > CONFIG_MAX_TASKS || ctx.found_self != 1 ||
		ctx.bad_tcb != 0) {
		dbg("sched_foreach returned invalid self enumeration result.\n");
		return ERROR;
	}

	if (worker_pid >= 0 && ctx.found_worker != 1) {
		dbg("sched_foreach did not enumerate worker thread.\n");
		return ERROR;
	}

	return OK;
}

static int test_sched_foreach_internal(unsigned long arg)
{
	struct tcb_s *self = sched_self();

	(void)arg;
	if (self == NULL || sched_gettcb(self->pid) != self) {
		return ERROR;
	}

	if (sched_gettcb(TEST_INVALID_PID) != NULL) {
		return ERROR;
	}

	return test_sched_foreach_verify(self, TEST_INVALID_PID);
}

static int test_task_setcancelstate(unsigned long arg)
{
	int noncancelable_flag;
	int originstate;
	int oldstate;
	int ret_chk;

	(void)arg;

	tcb = sched_self();
	if (tcb == NULL) {
		return ERROR;
	}

	noncancelable_flag = tcb->flags & TCB_FLAG_NONCANCELABLE;
	originstate = noncancelable_flag == TCB_FLAG_NONCANCELABLE ? TASK_CANCEL_DISABLE : TASK_CANCEL_ENABLE;

	set_errno(0);
	ret_chk = task_setcancelstate(TASK_CANCEL_INVALID, &oldstate);
	if (ret_chk != ERROR || get_errno() != EINVAL || oldstate != originstate) {
		goto errout;
	}

	ret_chk = task_setcancelstate(TASK_CANCEL_DISABLE, &oldstate);
	if (ret_chk != OK || (tcb->flags & TCB_FLAG_NONCANCELABLE) != TCB_FLAG_NONCANCELABLE || oldstate != originstate) {
		goto errout;
	}

	ret_chk = task_setcancelstate(TASK_CANCEL_ENABLE, &oldstate);
	if (ret_chk != OK || (tcb->flags & TCB_FLAG_NONCANCELABLE) != 0 || oldstate != TASK_CANCEL_DISABLE) {
		goto errout;
	}

	ret_chk = task_setcancelstate(originstate, NULL);
	if (ret_chk != OK || (tcb->flags & TCB_FLAG_NONCANCELABLE) != noncancelable_flag) {
		goto errout;
	}

	return OK;

errout:
	tcb->flags &= ~TCB_FLAG_NONCANCELABLE;
	tcb->flags |= noncancelable_flag;
	return ERROR;
}

#ifdef CONFIG_CANCELLATION_POINTS
static int test_task_setcanceltype(unsigned long arg)
{
	int deferred_flag;
	int origintype;
	int oldtype;
	int ret_chk;

	(void)arg;

	tcb = sched_self();
	if (tcb == NULL) {
		return ERROR;
	}

	deferred_flag = tcb->flags & TCB_FLAG_CANCEL_DEFERRED;
	origintype = deferred_flag == TCB_FLAG_CANCEL_DEFERRED ? TASK_CANCEL_DEFERRED : TASK_CANCEL_ASYNCHRONOUS;

	ret_chk = task_setcanceltype(TASK_CANCEL_INVALID, &oldtype);
	if (ret_chk != EINVAL || oldtype != origintype) {
		goto errout;
	}

	ret_chk = task_setcanceltype(TASK_CANCEL_DEFERRED, &oldtype);
	if (ret_chk != OK || (tcb->flags & TCB_FLAG_CANCEL_DEFERRED) != TCB_FLAG_CANCEL_DEFERRED || oldtype != origintype) {
		goto errout;
	}

	ret_chk = task_setcanceltype(TASK_CANCEL_ASYNCHRONOUS, &oldtype);
	if (ret_chk != OK || (tcb->flags & TCB_FLAG_CANCEL_DEFERRED) != 0 || oldtype != TASK_CANCEL_DEFERRED) {
		goto errout;
	}

	ret_chk = task_setcanceltype(origintype, NULL);
	if (ret_chk != OK || (tcb->flags & TCB_FLAG_CANCEL_DEFERRED) != deferred_flag) {
		goto errout;
	}

	return OK;

errout:
	tcb->flags &= ~TCB_FLAG_CANCEL_DEFERRED;
	tcb->flags |= deferred_flag;
	return ERROR;
}
#endif

static int test_sched_affinity(unsigned long arg)
{
	(void)arg;

#ifdef CONFIG_SMP
	cpu_set_t saved;
	cpu_set_t mask;
	int cpucount;
	int cpu;

	cpucount = sched_getcpucount();
	if (cpucount != CONFIG_SMP_NCPUS || cpucount <= 0) {
		dbg("sched_getcpucount returned %d.\n", cpucount);
		return ERROR;
	}

	cpu = sched_getcpu();
	if (cpu < 0 || cpu >= cpucount) {
		dbg("sched_getcpu returned %d.\n", cpu);
		return ERROR;
	}

	if (sched_getaffinity(0, sizeof(cpu_set_t), &saved) != OK) {
		dbg("sched_getaffinity failed.\n");
		return ERROR;
	}

	CPU_ZERO(&mask);
	CPU_SET(0, &mask);
	if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) != OK) {
		dbg("sched_setaffinity failed.\n");
		return ERROR;
	}

	CPU_ZERO(&mask);
	if (sched_getaffinity(0, sizeof(cpu_set_t), &mask) != OK ||
			!CPU_ISSET(0, &mask) || sched_cpucount(&mask) != 1) {
		dbg("sched_getaffinity returned invalid mask.\n");
		goto errout_restore;
	}

	if (sched_setaffinity(TEST_INVALID_PID, sizeof(cpu_set_t), &mask) != -ESRCH) {
		dbg("sched_setaffinity accepted invalid pid.\n");
		goto errout_restore;
	}

	if (sched_getaffinity(TEST_INVALID_PID, sizeof(cpu_set_t), &mask) != -ESRCH) {
		dbg("sched_getaffinity accepted invalid pid.\n");
		goto errout_restore;
	}

	set_errno(0);
	if (sched_setaffinity(0, 0, &mask) != ERROR || get_errno() != EINVAL) {
		dbg("sched_setaffinity accepted invalid cpusetsize.\n");
		goto errout_restore;
	}

	if (sched_setaffinity(0, sizeof(cpu_set_t), &saved) != OK) {
		dbg("sched_setaffinity restore failed.\n");
		return ERROR;
	}

	return OK;

errout_restore:
	sched_setaffinity(0, sizeof(cpu_set_t), &saved);
	return ERROR;
#else
	cpu_set_t mask = (cpu_set_t)1;

	if (sched_getcpucount() != 1) {
		return ERROR;
	}

	return sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -EINVAL ? OK : ERROR;
#endif
}

static int test_sched_state(unsigned long arg)
{
	struct tcb_s fake;
	struct tcb_s *self;
	cpu_set_t active;
	int cpucount;
#ifdef CONFIG_SMP
	int cpu;
#endif

	(void)arg;
	self = sched_self();
	if (self == NULL) {
		return ERROR;
	}

	if (sched_gettcb(self->pid) != self || sched_gettcb(TEST_INVALID_PID) != NULL) {
		return ERROR;
	}

	if (sched_verifytcb(self) != true) {
		return ERROR;
	}

	memset(&fake, 0, sizeof(struct tcb_s));
	fake.pid = self->pid;
	if (sched_verifytcb(&fake) != false) {
		return ERROR;
	}

	cpucount = sched_getcpucount();
#ifdef CONFIG_SMP
	cpu = sched_getcpu();
	active = sched_getactivecpu();
	if (cpucount <= 0 || cpu < 0 || cpu >= cpucount) {
		return ERROR;
	}

	if ((active & ((cpu_set_t)1 << cpu)) == 0) {
		return ERROR;
	}
#else
	active = sched_getactivecpu();
	if (cpucount != 1 || active != (cpu_set_t)1) {
		return ERROR;
	}
#endif

	return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_sched(int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	switch (cmd) {
	case TESTIOC_GET_SELF_PID:
		ret = test_get_self_pid(arg);
		break;
	case TESTIOC_IS_ALIVE_THREAD:
		ret = test_is_alive_thread(arg);
		break;
	case TESTIOC_GET_TCB_ADJ_STACK_SIZE:
		ret = test_get_tcb_adj_stack_size(arg);
		break;
	case TESTIOC_SCHED_FOREACH:
		ret = test_sched_foreach(arg);
		break;
	case TESTIOC_SCHED_FOREACH_TEST:
		ret = test_sched_foreach_internal(arg);
		break;
	case TESTIOC_TASK_SETCANCELSTATE_TEST:
		ret = test_task_setcancelstate(arg);
		break;
#ifdef CONFIG_CANCELLATION_POINTS
	case TESTIOC_TASK_SETCANCELTYPE_TEST:
		ret = test_task_setcanceltype(arg);
		break;
#endif
	case TESTIOC_SCHED_AFFINITY_TEST:
		ret = test_sched_affinity(arg);
		break;
	case TESTIOC_SCHED_STATE_TEST:
		ret = test_sched_state(arg);
		break;
	}
	return ret;
}
