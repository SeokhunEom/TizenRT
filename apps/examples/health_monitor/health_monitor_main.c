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
#include <tinyara/config.h>
#include <tinyara/health_monitor.h>
#include <tinyara/clock.h>
#include <tinyara/arch.h>
#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(CONFIG_BUILD_FLAT) || defined(CONFIG_BUILD_PROTECTED) || defined(CONFIG_BUILD_KERNEL)
#error "health_monitor example requires a flat build (no user syscall ABI)"
#endif

#define HM_DEFAULT_TIMEOUT 2000
#define HM_DEFAULT_CYCLES 5
#define HM_RETRIES 3
#define HM_TICK_MS ((USEC_PER_TICK + 999) / 1000)

enum hm_operation_e { HM_START, HM_KICK, HM_STOP };

static void hm_usage(void)
{
	printf("Usage:\n"
		   "  health_monitor healthy|restart|migrate [cycles=5] [timeout_ms=2000]\n"
		   "  health_monitor errors\n"
		   "  health_monitor timeout|exit [timeout_ms=2000]\n"
		   "cycles: 1..1000; timeout_ms: 100..60000 and at least 8 ticks.\n"
		   "migrate requires SMP. timeout/exit intentionally cause fatal diagnostics.\n"
		   "A stalled dump is not guaranteed to reset without a board HW watchdog.\n");
}

static bool hm_number(const char *text, unsigned long min, unsigned long max, uint32_t *out)
{
	unsigned long value = 0;
	if (!*text) {
		return false;
	}
	while (*text) {
		unsigned int digit;
		if (*text < '0' || *text > '9') {
			return false;
		}
		digit = (unsigned int)(*text++ - '0');
		if (value > max / 10 || (value == max / 10 && digit > max % 10)) {
			return false;
		}
		value = value * 10 + digit;
	}
	if (value < min) {
		return false;
	}
	*out = (uint32_t)value;
	return true;
}

static int hm_wait(uint32_t ms)
{
	struct timespec delay;
	delay.tv_sec = ms / 1000;
	delay.tv_nsec = (ms % 1000) * 1000000L;
	if (nanosleep(&delay, NULL) < 0 && errno != EINTR) {
		return -errno;
	}
	/* An interrupted wait is allowed: completed work still gets a kick. */
	return 0;
}

static int hm_call(enum hm_operation_e op, uint32_t timeout)
{
	int ret = -EAGAIN;
	unsigned int attempt;
	for (attempt = 0; attempt < HM_RETRIES; attempt++) {
		ret = op == HM_START ? health_monitor_start(timeout) :
			(op == HM_KICK ? health_monitor_kick() : health_monitor_stop());
		if ((ret != -EAGAIN && ret != -ENOSPC) || attempt + 1 == HM_RETRIES) {
			break;
		}
		/* Bounded, sleeping retries. A failed stop is still armed. */
		int wait_ret = hm_wait(HM_TICK_MS);
		if (wait_ret) {
			return wait_ret;
		}
	}
	return ret;
}

static int hm_result(const char *mode, int ret, uint32_t completed)
{
	printf("RESULT health_monitor %s %s completed=%lu ret=%d\n",
		   mode, ret ? "FAIL" : "PASS", (unsigned long)completed, ret);
	return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int hm_errors(void)
{
	int ret;
	bool armed = false;
	uint32_t checks = 0;
	if (health_monitor_kick() != -ENOENT || health_monitor_stop() != -ENOENT ||
		health_monitor_start(0) != -EINVAL) {
		return hm_result("errors", -EIO, checks);
	}
	checks = 3;
	ret = hm_call(HM_START, HM_DEFAULT_TIMEOUT);
	if (ret) {
		return hm_result("errors", ret, checks);
	}
	armed = true;
	if (health_monitor_start(HM_DEFAULT_TIMEOUT) != -EALREADY) {
		ret = -EIO;
		goto cleanup;
	}
	checks++;
	ret = hm_call(HM_KICK, 0);
	if (ret) {
		goto cleanup;
	}
	checks++;
	ret = hm_call(HM_STOP, 0);
	if (ret) {
		goto cleanup;
	}
	armed = false;
	checks++;
	if (health_monitor_stop() != -ENOENT || health_monitor_kick() != -ENOENT) {
		ret = -EIO;
	} else {
		checks += 2;
	}
cleanup:
	if (armed) {
		int stop_ret = hm_call(HM_STOP, 0);
		if (stop_ret) {
			printf("Cleanup stop failed (%d): contract remains armed.\n", stop_ret);
		}
	}
	return hm_result("errors", ret, checks);
}

static int hm_run(const char *mode, uint32_t cycles, uint32_t timeout)
{
	bool scoped = strcmp(mode, "restart") == 0;
	bool migrate = strcmp(mode, "migrate") == 0;
	bool armed = false;
	uint32_t completed = 0;
	volatile uint32_t work = 1;
	int ret = 0;
#ifdef CONFIG_SMP
	cpu_set_t saved;
	bool affinity_saved = false;
	if (migrate) {
		if (CONFIG_SMP_NCPUS < 2 || cycles < 2) {
			return hm_result(mode, -EINVAL, 0);
		}
		ret = sched_getaffinity(0, sizeof(saved), &saved);
		if (ret) {
			return hm_result(mode, ret, 0);
		}
		affinity_saved = true;
	}
#else
	if (migrate) {
		return hm_result(mode, -ENOTSUP, 0);
	}
#endif
	for (uint32_t cycle = 0; cycle < cycles; cycle++) {
#ifdef CONFIG_SMP
		if (migrate) {
			cpu_set_t target;
			unsigned int next_cpu = cycle % CONFIG_SMP_NCPUS;
			CPU_ZERO(&target);
			CPU_SET(next_cpu, &target);
			ret = sched_setaffinity(0, sizeof(target), &target);
			if (ret) {
				goto cleanup;
			}
			if ((unsigned int)up_cpu_index() != next_cpu) {
				ret = -EIO;
				goto cleanup;
			}
		}
#endif
		if (!armed) {
			ret = hm_call(HM_START, timeout);
			if (ret) {
				goto cleanup;
			}
			armed = true;
		}
		/* A small completed work batch stands in for a module checkpoint.
		 * Do not printf while armed: console stalls would become test faults.
		 */
		for (unsigned int i = 0; i < 256; i++) {
			work = work * 1664525U + 1013904223U;
		}
		ret = hm_wait(timeout / 4);
		if (ret) {
			goto cleanup;
		}
		ret = hm_call(HM_KICK, 0);
		if (ret) {
			goto cleanup;
		}
		if (scoped) {
			ret = hm_call(HM_STOP, 0);
			if (ret) {
				goto cleanup;
			}
			armed = false;
			ret = hm_wait(timeout / 4);
			if (ret) {
				goto cleanup;
			}
		}
		completed++;
	}
cleanup:
	if (armed) {
		int stop_ret = hm_call(HM_STOP, 0);
		if (stop_ret) {
			printf("Cleanup stop failed (%d): contract remains armed.\n", stop_ret);
			if (!ret) {
				ret = stop_ret;
			}
		}
	}
#ifdef CONFIG_SMP
	if (affinity_saved) {
		int restore_ret = sched_setaffinity(0, sizeof(saved), &saved);
		if (!ret) {
			ret = restore_ret;
		}
	}
#endif
	return hm_result(mode, ret, completed);
}

int health_monitor_main(int argc, char *argv[])
{
	uint32_t cycles = HM_DEFAULT_CYCLES;
	uint32_t timeout = HM_DEFAULT_TIMEOUT;
	bool fatal;
	const char *mode;
	int ret;
	if (argc == 1 || (argc == 2 && strcmp(argv[1], "help") == 0)) {
		hm_usage();
		return EXIT_SUCCESS;
	}
	if (argc < 2) {
		return EXIT_FAILURE;
	}
	mode = argv[1];
	fatal = strcmp(mode, "timeout") == 0 || strcmp(mode, "exit") == 0;
	if (strcmp(mode, "errors") == 0) {
		if (argc != 2) {
			hm_usage();
			return EXIT_FAILURE;
		}
		return hm_errors();
	}
	if (fatal) {
		if (argc > 3 || (argc == 3 && !hm_number(argv[2], 100, 60000, &timeout))) {
			hm_usage();
			return EXIT_FAILURE;
		}
	} else if (strcmp(mode, "healthy") == 0 || strcmp(mode, "restart") == 0 || strcmp(mode, "migrate") == 0) {
		if (argc > 4 || (argc >= 3 && !hm_number(argv[2], 1, 1000, &cycles)) ||
			(argc == 4 && !hm_number(argv[3], 100, 60000, &timeout))) {
			hm_usage();
			return EXIT_FAILURE;
		}
	} else {
		hm_usage();
		return EXIT_FAILURE;
	}
	if ((uint64_t)timeout * 1000 < (uint64_t)USEC_PER_TICK * 8) {
		printf("Timeout must cover at least 8 system ticks.\n");
		return EXIT_FAILURE;
	}
	if (!fatal) {
		printf("RUN health_monitor %s cycles=%lu timeout_ms=%lu\n", mode,
			   (unsigned long)cycles, (unsigned long)timeout);
		fflush(stdout);
		return hm_run(mode, cycles, timeout);
	}
	printf("EXPECT_FATAL health_monitor %s reason=%u timeout_ms=%lu\n",
		   mode, strcmp(mode, "timeout") == 0 ? 1 : 2, (unsigned long)timeout);
	fflush(stdout);
	ret = hm_call(HM_START, timeout);
	if (ret) {
		return hm_result(mode, ret, 0);
	}
	if (strcmp(mode, "exit") == 0) {
		/* Deliberately return without stop. Do not print PASS. The task's
		 * release hook must publish reason 2 and CPU0 must print HM fault.
		 */
		return EXIT_SUCCESS;
	}
	for (;;) {
		/* Keep interrupts enabled. The CPU0 tick must diagnose reason 1.
		 * No stop/kick is permitted in this intentional timeout scenario.
		 */
		(void)hm_wait(timeout);
	}
}
