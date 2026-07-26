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

#ifndef CONFIG_DISABLE_ENVIRON

#include <debug.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <tinyara/os_api_test_drv.h>
#include <tinyara/sched.h>

#include "environ/environ.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_ENV_NAME		"OSAPI_ENV"
#define TEST_ENV_VALUE		"one"
#define TEST_ENV_NEW_VALUE	"two"
#define TEST_ENV_PUT_NAME	"OSAPI_PUT"
#define TEST_ENV_PUT_VALUE	"put"
#define TEST_ENV_PUT_STRING	"OSAPI_PUT=put"
#define TEST_ENV_REMOVE_NAME	"OSAPI_REMOVE"
#define TEST_ENV_KEEP_NAME	"OSAPI_KEEP"
#define TEST_ENV_KEEP_VALUE	"keep"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_environ_get_empty(void)
{
	size_t envsize = 1;

	set_errno(0);
	if (get_environ_ptr(NULL) != NULL || get_errno() != EINVAL) {
		dbg("get_environ_ptr accepted NULL size pointer.\n");
		return ERROR;
	}

	set_errno(0);
	if (get_environ_ptr(&envsize) != NULL || envsize != 0 || get_errno() != ENOENT) {
		dbg("get_environ_ptr returned unexpected empty environment state.\n");
		return ERROR;
	}

	return OK;
}

static int test_environ_public_ops(FAR struct task_group_s *group)
{
	FAR char *envp;
	FAR char *value;
	size_t envsize;
	size_t expected_size;

	if (setenv(TEST_ENV_NAME, TEST_ENV_VALUE, TRUE) != OK) {
		dbg("setenv failed.\n");
		return ERROR;
	}

	value = getenv(TEST_ENV_NAME);
	if (value == NULL || strcmp(value, TEST_ENV_VALUE) != 0) {
		dbg("getenv returned unexpected value.\n");
		return ERROR;
	}

	expected_size = strlen(TEST_ENV_NAME) + strlen(TEST_ENV_VALUE) + 2;
	envp = get_environ_ptr(&envsize);
	if (envp != group->tg_envp || envsize != expected_size) {
		dbg("get_environ_ptr returned unexpected environment buffer.\n");
		return ERROR;
	}

	if (setenv(TEST_ENV_NAME, TEST_ENV_NEW_VALUE, FALSE) != OK) {
		dbg("setenv overwrite false failed.\n");
		return ERROR;
	}

	value = getenv(TEST_ENV_NAME);
	if (value == NULL || strcmp(value, TEST_ENV_VALUE) != 0) {
		dbg("setenv overwrite false changed existing value.\n");
		return ERROR;
	}

	if (setenv(TEST_ENV_NAME, TEST_ENV_NEW_VALUE, TRUE) != OK) {
		dbg("setenv overwrite true failed.\n");
		return ERROR;
	}

	value = getenv(TEST_ENV_NAME);
	if (value == NULL || strcmp(value, TEST_ENV_NEW_VALUE) != 0) {
		dbg("setenv overwrite true did not change value.\n");
		return ERROR;
	}

	if (putenv(TEST_ENV_PUT_STRING) != OK) {
		dbg("putenv failed.\n");
		return ERROR;
	}

	value = getenv(TEST_ENV_PUT_NAME);
	if (value == NULL || strcmp(value, TEST_ENV_PUT_VALUE) != 0) {
		dbg("putenv value lookup failed.\n");
		return ERROR;
	}

	if (setenv(TEST_ENV_PUT_NAME, NULL, TRUE) != OK) {
		dbg("setenv NULL value unset failed.\n");
		return ERROR;
	}

	set_errno(0);
	if (getenv(TEST_ENV_PUT_NAME) != NULL || get_errno() != ENOENT) {
		dbg("setenv NULL value did not unset variable.\n");
		return ERROR;
	}

	return OK;
}

static int test_environ_internal_remove(FAR struct task_group_s *group)
{
	FAR char *found;

	if (setenv(TEST_ENV_REMOVE_NAME, TEST_ENV_VALUE, TRUE) != OK ||
		setenv(TEST_ENV_KEEP_NAME, TEST_ENV_KEEP_VALUE, TRUE) != OK) {
		dbg("setenv for remove test failed.\n");
		return ERROR;
	}

	sched_lock();
	found = env_findvar(group, TEST_ENV_REMOVE_NAME);
	if (found == NULL || strcmp(found, TEST_ENV_REMOVE_NAME "=" TEST_ENV_VALUE) != 0) {
		sched_unlock();
		dbg("env_findvar did not find expected variable.\n");
		return ERROR;
	}

	if (env_removevar(group, found) != OK) {
		sched_unlock();
		dbg("env_removevar failed.\n");
		return ERROR;
	}

	if (env_findvar(group, TEST_ENV_REMOVE_NAME) != NULL ||
		env_findvar(group, TEST_ENV_KEEP_NAME) == NULL) {
		sched_unlock();
		dbg("env_removevar left unexpected environment state.\n");
		return ERROR;
	}

	sched_unlock();
	return OK;
}

static int test_environ_dup(FAR struct task_group_s *group)
{
	struct task_group_s child_group;
	int ret;

	memset(&child_group, 0, sizeof(child_group));
	ret = env_dup(&child_group);
	if (ret != OK) {
		dbg("env_dup failed.\n");
		return ERROR;
	}

	if (child_group.tg_envsize != group->tg_envsize ||
		child_group.tg_envp == NULL ||
		child_group.tg_envp == group->tg_envp ||
		memcmp(child_group.tg_envp, group->tg_envp, group->tg_envsize) != 0) {
		dbg("env_dup returned unexpected environment copy.\n");
		env_release(&child_group);
		return ERROR;
	}

	env_release(&child_group);
	if (child_group.tg_envp != NULL || child_group.tg_envsize != 0) {
		dbg("env_release did not clear duplicated environment.\n");
		return ERROR;
	}

	return OK;
}

static int test_environ_all(unsigned long arg)
{
	FAR struct tcb_s *self;
	FAR struct task_group_s *group;
	struct task_group_s test_group;
	FAR char *saved_envp;
	FAR char *test_envp;
	size_t saved_envsize;
	size_t test_envsize;
	int ret = ERROR;

	(void)arg;

	self = sched_self();
	if (self == NULL || self->group == NULL) {
		dbg("sched_self failed.\n");
		return ERROR;
	}

	group = self->group;
	sched_lock();
	saved_envp = group->tg_envp;
	saved_envsize = group->tg_envsize;
	group->tg_envp = NULL;
	group->tg_envsize = 0;
	sched_unlock();

	if (test_environ_get_empty() != OK) {
		goto out;
	}

	if (test_environ_public_ops(group) != OK) {
		goto out;
	}

	if (test_environ_internal_remove(group) != OK) {
		goto out;
	}

	if (test_environ_dup(group) != OK) {
		goto out;
	}

	ret = OK;

out:
	sched_lock();
	test_envp = group->tg_envp;
	test_envsize = group->tg_envsize;
	group->tg_envp = saved_envp;
	group->tg_envsize = saved_envsize;
	sched_unlock();

	memset(&test_group, 0, sizeof(test_group));
	test_group.tg_envp = test_envp;
	test_group.tg_envsize = test_envsize;
	env_release(&test_group);

	return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_environ(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_ENVIRON_TEST:
		ret = test_environ_all(arg);
		break;
	}

	return ret;
}

#endif							/* CONFIG_DISABLE_ENVIRON */
