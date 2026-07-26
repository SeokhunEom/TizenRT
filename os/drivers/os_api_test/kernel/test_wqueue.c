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
#include <semaphore.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <tinyara/os_api_test_drv.h>
#include <tinyara/wqueue.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TEST_WORK_DELAY_TICKS	50
#define TEST_WORK_WAIT_SEC	2

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct test_wqueue_s {
	sem_t done;
	volatile int run_count;
	volatile int cancel_count;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_SCHED_WORKQUEUE) && (defined(CONFIG_SCHED_HPWORK) || defined(CONFIG_SCHED_LPWORK))
static void test_wqueue_worker(FAR void *arg)
{
	FAR struct test_wqueue_s *ctx = (FAR struct test_wqueue_s *)arg;

	ctx->run_count++;
	sem_post(&ctx->done);
}

static void test_wqueue_cancel_worker(FAR void *arg)
{
	FAR struct test_wqueue_s *ctx = (FAR struct test_wqueue_s *)arg;

	ctx->cancel_count++;
	sem_post(&ctx->done);
}

static int test_wqueue_wait(FAR sem_t *sem)
{
	struct timespec abstime;

	if (clock_gettime(CLOCK_REALTIME, &abstime) != OK) {
		return ERROR;
	}

	abstime.tv_sec += TEST_WORK_WAIT_SEC;
	return sem_timedwait(sem, &abstime);
}

static int test_wqueue_queue(int qid)
{
	struct test_wqueue_s ctx;
	struct work_s work;
	int ret;

	memset(&ctx, 0, sizeof(ctx));
	memset(&work, 0, sizeof(work));

	ret = sem_init(&ctx.done, 0, 0);
	if (ret != OK) {
		return ERROR;
	}

	ret = work_queue(qid, &work, test_wqueue_worker, &ctx, 0);
	if (ret != OK) {
		goto errout;
	}

	ret = test_wqueue_wait(&ctx.done);
	if (ret != OK || ctx.run_count != 1) {
		ret = ERROR;
		goto errout;
	}

	if (work.worker != NULL) {
		ret = ERROR;
		goto errout;
	}

	ret = work_cancel(qid, &work);
	if (ret != -ENOENT) {
		ret = ERROR;
		goto errout;
	}

	ret = OK;

errout:
	work_cancel(qid, &work);
	sem_destroy(&ctx.done);
	return ret;
}

static int test_wqueue_cancel_missing(int qid)
{
	struct work_s work;
	int ret;

	memset(&work, 0, sizeof(work));

	ret = work_cancel(-1, &work);
	if (ret != -EINVAL) {
		return ERROR;
	}

	ret = work_cancel(qid, &work);
	if (ret != -ENOENT) {
		return ERROR;
	}

	return OK;
}
#endif

static int test_work_queue_cancel(unsigned long arg)
{
	(void)arg;

#if defined(CONFIG_SCHED_WORKQUEUE) && (defined(CONFIG_SCHED_HPWORK) || defined(CONFIG_SCHED_LPWORK))
	struct test_wqueue_s ctx;
	struct work_s cancel_work;
	struct work_s invalid_work;
	int cancel_qid = -1;
	int ret;

	memset(&ctx, 0, sizeof(ctx));
	memset(&cancel_work, 0, sizeof(cancel_work));
	memset(&invalid_work, 0, sizeof(invalid_work));

	ret = sem_init(&ctx.done, 0, 0);
	if (ret != OK) {
		return ERROR;
	}

	ret = work_queue(-1, &invalid_work, test_wqueue_worker, &ctx, 0);
	if (ret != -EINVAL) {
		ret = ERROR;
		goto errout;
	}

#ifdef CONFIG_SCHED_HPWORK
	ret = test_wqueue_cancel_missing(HPWORK);
	if (ret != OK) {
		goto errout;
	}

	ret = test_wqueue_queue(HPWORK);
	if (ret != OK) {
		goto errout;
	}

	ret = work_queue(HPWORK, &cancel_work, test_wqueue_cancel_worker, &ctx, TEST_WORK_DELAY_TICKS);
	if (ret != OK) {
		goto errout;
	}
	cancel_qid = HPWORK;

	ret = work_queue(HPWORK, &cancel_work, test_wqueue_cancel_worker, &ctx, TEST_WORK_DELAY_TICKS);
	if (ret != -EALREADY) {
		goto errout;
	}

	ret = work_cancel(HPWORK, &cancel_work);
	if (ret != OK) {
		goto errout;
	}
	cancel_qid = -1;

	ret = work_cancel(HPWORK, &cancel_work);
	if (ret != -ENOENT) {
		goto errout;
	}
#else
	ret = test_wqueue_cancel_missing(LPWORK);
	if (ret != OK) {
		goto errout;
	}

	ret = test_wqueue_queue(LPWORK);
	if (ret != OK) {
		goto errout;
	}

	ret = work_queue(LPWORK, &cancel_work, test_wqueue_cancel_worker, &ctx, TEST_WORK_DELAY_TICKS);
	if (ret != OK) {
		goto errout;
	}
	cancel_qid = LPWORK;

	ret = work_queue(LPWORK, &cancel_work, test_wqueue_cancel_worker, &ctx, TEST_WORK_DELAY_TICKS);
	if (ret != -EALREADY) {
		goto errout;
	}

	ret = work_cancel(LPWORK, &cancel_work);
	if (ret != OK) {
		goto errout;
	}
	cancel_qid = -1;

	ret = work_cancel(LPWORK, &cancel_work);
	if (ret != -ENOENT) {
		goto errout;
	}
#endif

#ifdef CONFIG_SCHED_LPWORK
	ret = test_wqueue_queue(LPWORK);
	if (ret != OK) {
		goto errout;
	}
#endif

	sleep(1);
	if (ctx.cancel_count != 0) {
		ret = ERROR;
		goto errout;
	}

	ret = OK;

errout:
	if (cancel_qid >= 0) {
		work_cancel(cancel_qid, &cancel_work);
	}
	sem_destroy(&ctx.done);
	return ret;
#else
	return -ENOSYS;
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_wqueue(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_WORK_QUEUE_TEST:
		ret = test_work_queue_cancel(arg);
		break;
	}

	return ret;
}
