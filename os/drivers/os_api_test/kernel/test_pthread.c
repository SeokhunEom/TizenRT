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
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <debug.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>

#include <tinyara/os_api_test_drv.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_pthread_mutex(void)
{
	pthread_mutex_t mutex;
	int ret;

	ret = pthread_mutex_init(&mutex, NULL);
	if (ret != OK) {
		dbg("pthread_mutex_init failed: %d\n", ret);
		return ERROR;
	}

	ret = pthread_mutex_lock(&mutex);
	if (ret != OK) {
		dbg("pthread_mutex_lock failed: %d\n", ret);
		goto errout;
	}

	ret = pthread_mutex_trylock(&mutex);
	if (ret != EBUSY) {
		dbg("pthread_mutex_trylock returned %d for locked mutex.\n", ret);
		pthread_mutex_unlock(&mutex);
		goto errout;
	}

	ret = pthread_mutex_unlock(&mutex);
	if (ret != OK) {
		dbg("pthread_mutex_unlock failed: %d\n", ret);
		goto errout;
	}

	ret = pthread_mutex_trylock(&mutex);
	if (ret != OK) {
		dbg("pthread_mutex_trylock failed for unlocked mutex: %d\n", ret);
		goto errout;
	}

	ret = pthread_mutex_unlock(&mutex);
	if (ret != OK) {
		dbg("pthread_mutex_unlock after trylock failed: %d\n", ret);
		goto errout;
	}

	ret = pthread_mutex_destroy(&mutex);
	if (ret != OK) {
		dbg("pthread_mutex_destroy failed: %d\n", ret);
		return ERROR;
	}

	return OK;

errout:
	pthread_mutex_destroy(&mutex);
	return ERROR;
}

static int test_pthread_schedparam(void)
{
	struct sched_param param;
	pthread_t self = pthread_self();
	int policy;
	int ret;

	ret = pthread_getschedparam(self, &policy, &param);
	if (ret != OK) {
		dbg("pthread_getschedparam failed: %d\n", ret);
		return ERROR;
	}

	ret = pthread_setschedparam(self, policy, &param);
	if (ret != OK) {
		dbg("pthread_setschedparam failed: %d\n", ret);
		return ERROR;
	}

	ret = pthread_setschedprio(self, param.sched_priority);
	if (ret != OK) {
		dbg("pthread_setschedprio failed: %d\n", ret);
		return ERROR;
	}

	return OK;
}

static int test_pthread_all(unsigned long arg)
{
	(void)arg;

	if (test_pthread_mutex() != OK) {
		return ERROR;
	}

	if (test_pthread_schedparam() != OK) {
		return ERROR;
	}

	return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_pthread(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_PTHREAD_TEST:
		ret = test_pthread_all(arg);
		break;
	}

	return ret;
}
