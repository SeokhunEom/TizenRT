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

#include <debug.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <tinyara/irq.h>
#include <tinyara/os_api_test_drv.h>
#include <tinyara/pm/pm.h>

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_PM_DOMAIN_NAME "os_api_test_pm"
#define TEST_PM_TIMED_SUSPEND_MS 1000

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_pm_invalid_args(void)
{
	set_errno(0);
	if (pm_domain_register(NULL) != NULL || get_errno() != EINVAL) {
		dbg("pm_domain_register accepted NULL name.\n");
		return ERROR;
	}

	set_errno(0);
	if (pm_domain_register("") != NULL || get_errno() != E2BIG) {
		dbg("pm_domain_register accepted empty name.\n");
		return ERROR;
	}

	set_errno(0);
	if (pm_suspend(NULL) != ERROR || get_errno() != EINVAL) {
		dbg("pm_suspend accepted NULL domain.\n");
		return ERROR;
	}

	set_errno(0);
	if (pm_resume(NULL) != ERROR || get_errno() != EINVAL) {
		dbg("pm_resume accepted NULL domain.\n");
		return ERROR;
	}

	set_errno(0);
	if (pm_suspendcount(NULL) != ERROR || get_errno() != EINVAL) {
		dbg("pm_suspendcount accepted NULL domain.\n");
		return ERROR;
	}

	set_errno(0);
	if (pm_domain_unregister(NULL) != ERROR || get_errno() != EINVAL) {
		dbg("pm_domain_unregister accepted NULL domain.\n");
		return ERROR;
	}

	set_errno(0);
	if (pm_timedsuspend(NULL, 1) != ERROR || get_errno() != EINVAL) {
		dbg("pm_timedsuspend accepted NULL domain.\n");
		return ERROR;
	}

#ifdef CONFIG_PM_METRICS
	set_errno(0);
	if (pm_metrics(-1) != ERROR || get_errno() != EINVAL) {
		dbg("pm_metrics accepted invalid duration.\n");
		return ERROR;
	}
#endif

	return OK;
}

static int test_pm_domain_name_boundary(void)
{
	char long_name[CONFIG_PM_DOMAIN_NAME_SIZE + 1];

	memset(long_name, 'p', sizeof(long_name));
	long_name[CONFIG_PM_DOMAIN_NAME_SIZE] = '\0';

	set_errno(0);
	if (pm_domain_register(long_name) != NULL || get_errno() != E2BIG) {
		dbg("pm_domain_register accepted long domain name.\n");
		return ERROR;
	}

	return OK;
}

static int test_pm_domain_initial_state(FAR struct pm_domain_s *domain)
{
	if (strncmp(domain->name, TEST_PM_DOMAIN_NAME, CONFIG_PM_DOMAIN_NAME_SIZE) != 0) {
		dbg("pm_domain_register stored unexpected name.\n");
		return ERROR;
	}

	if (domain->suspend_count != 0 || domain->wdog != NULL) {
		dbg("pm_domain_register initialized unexpected state.\n");
		return ERROR;
	}

#ifdef CONFIG_PM_METRICS
	if (domain->blocking_board_sleep_ticks != 0 || domain->suspend_ticks != 0) {
		dbg("pm_domain_register initialized unexpected metrics.\n");
		return ERROR;
	}
#endif

	return OK;
}

static int test_pm_domain_boundaries(FAR struct pm_domain_s *domain)
{
	irqstate_t flags;
	int ret;
	int suspend_count;

	flags = enter_critical_section();
	domain->suspend_count = UINT16_MAX;
	set_errno(0);
	ret = pm_suspend(domain);
	suspend_count = domain->suspend_count;
	domain->suspend_count = 0;
	leave_critical_section(flags);

	if (ret != ERROR || get_errno() != ERANGE || suspend_count != UINT16_MAX) {
		dbg("pm_suspend accepted saturated suspend count.\n");
		return ERROR;
	}

	if (pm_timedsuspend(domain, 0) != OK || domain->suspend_count != 0 ||
		domain->wdog != NULL) {
		dbg("pm_timedsuspend zero timeout changed inactive domain.\n");
		return ERROR;
	}

	return OK;
}

static int test_pm_domain_timed_suspend(FAR struct pm_domain_s *domain)
{
	if (pm_timedsuspend(domain, TEST_PM_TIMED_SUSPEND_MS) != OK) {
		dbg("pm_timedsuspend failed.\n");
		return ERROR;
	}

	if (pm_suspendcount(domain) != 1 || domain->wdog == NULL ||
		pm_checkstate() != PM_NORMAL) {
		dbg("pm_timedsuspend did not suspend domain with watchdog.\n");
		(void)pm_timedsuspend(domain, 0);
		return ERROR;
	}

	if (pm_timedsuspend(domain, 0) != OK) {
		dbg("pm_timedsuspend zero timeout failed to cancel.\n");
		return ERROR;
	}

	if (pm_suspendcount(domain) != 0 || domain->wdog != NULL) {
		dbg("pm_timedsuspend zero timeout did not resume and cleanup.\n");
		return ERROR;
	}

	return OK;
}

static int test_pm_domain_lifecycle(void)
{
	FAR struct pm_domain_s *domain;
	FAR struct pm_domain_s *same_domain;
	int ret = ERROR;

	domain = pm_domain_register(TEST_PM_DOMAIN_NAME);
	if (domain == NULL) {
		dbg("pm_domain_register failed.\n");
		return ERROR;
	}

	if (test_pm_domain_initial_state(domain) != OK) {
		goto errout_with_domain;
	}

	same_domain = pm_domain_register(TEST_PM_DOMAIN_NAME);
	if (same_domain != domain) {
		dbg("pm_domain_register did not return existing domain.\n");
		goto errout_with_domain;
	}

	if (test_pm_domain_boundaries(domain) != OK) {
		goto errout_with_domain;
	}

	if (test_pm_domain_timed_suspend(domain) != OK) {
		goto errout_with_domain;
	}

	if (pm_suspendcount(domain) != 0) {
		dbg("new PM domain has nonzero suspend count.\n");
		goto errout_with_domain;
	}

	if (pm_suspend(domain) != OK) {
		dbg("pm_suspend failed.\n");
		goto errout_with_domain;
	}

	if (pm_suspendcount(domain) != 1) {
		dbg("pm_suspend did not increment suspend count.\n");
		goto errout_resume_domain;
	}

	if (pm_checkstate() != PM_NORMAL) {
		dbg("pm_checkstate did not observe suspended domain.\n");
		goto errout_resume_domain;
	}

	if (pm_resume(domain) != OK) {
		dbg("pm_resume failed.\n");
		goto errout_with_domain;
	}

	if (pm_suspendcount(domain) != 0) {
		dbg("pm_resume did not decrement suspend count.\n");
		goto errout_with_domain;
	}

	set_errno(0);
	if (pm_resume(domain) != ERROR || get_errno() != ERANGE) {
		dbg("pm_resume accepted zero suspend count.\n");
		goto errout_with_domain;
	}

	ret = OK;
	goto errout_with_domain;

errout_resume_domain:
	(void)pm_resume(domain);

errout_with_domain:
	if (pm_domain_unregister(domain) != OK) {
		dbg("pm_domain_unregister failed.\n");
		return ERROR;
	}

	return ret;
}

static int test_pm_core(unsigned long arg)
{
	(void)arg;

	if (test_pm_invalid_args() != OK) {
		return ERROR;
	}

	if (test_pm_domain_name_boundary() != OK) {
		return ERROR;
	}

	return test_pm_domain_lifecycle();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_pm(int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case TESTIOC_PM_TEST:
		ret = test_pm_core(arg);
		break;
	}

	return ret;
}
