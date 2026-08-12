/****************************************************************************
 * apps/examples/ltp/ltp_runner.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <tinyara/config.h>

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "posixtest.h"

#define LTP_TEST(command, entry) int entry(int argc, char *argv[]);
#include "ltp_registry.inc"
#undef LTP_TEST

struct ltp_test_s {
	const char *command;
	main_t entry;
};

#define LTP_TEST(command, entry) { command, entry },
static const struct ltp_test_s g_ltp_tests[] = {
#include "ltp_registry.inc"
};
#undef LTP_TEST

static const char *ltp_status_name(int status)
{
	switch (status) {
	case PTS_PASS:
		return "PASS";
	case PTS_FAIL:
		return "FAIL";
	case PTS_UNRESOLVED:
		return "UNRESOLVED";
	case PTS_UNSUPPORTED:
		return "UNSUPPORTED";
	case PTS_UNTESTED:
		return "UNTESTED";
	default:
		return "UNKNOWN";
	}
}

int ltp_runner_main(int argc, char *argv[])
{
	const struct ltp_test_s *test = NULL;
	const char *command;
	pid_t child;
	pid_t waited;
	int status;
	int exit_status;
	size_t index;

	if (argc < 1 || argv == NULL || argv[0] == NULL) {
		printf("LTP_RESULT unknown ERROR reason=missing-command\n");
		return PTS_UNRESOLVED;
	}

	command = argv[0];
	for (index = 0; index < sizeof(g_ltp_tests) / sizeof(g_ltp_tests[0]); index++) {
		if (strcmp(command, g_ltp_tests[index].command) == 0) {
			test = &g_ltp_tests[index];
			break;
		}
	}

	if (test == NULL) {
		printf("LTP_RESULT %s ERROR reason=unknown-command\n", command);
		return PTS_UNRESOLVED;
	}

	child = task_create(command, CONFIG_EXAMPLES_LTP_PRIORITY,
			CONFIG_EXAMPLES_LTP_STACKSIZE, test->entry, NULL);
	if (child < 0) {
		printf("LTP_RESULT %s ERROR reason=task-create errno=%d\n",
			command, errno);
		return PTS_UNRESOLVED;
	}

	waited = waitpid(child, &status, 0);
	if (waited != child || !WIFEXITED(status)) {
		printf("LTP_RESULT %s ERROR reason=waitpid waited=%d errno=%d status=%d\n",
			command, (int)waited, errno, status);
		return PTS_UNRESOLVED;
	}

	exit_status = WEXITSTATUS(status);
	printf("LTP_RESULT %s %s exit=%d\n", command,
		ltp_status_name(exit_status), exit_status);
	return exit_status;
}
