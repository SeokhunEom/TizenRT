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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#include <tinyara/config.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <tinyara/health_monitor.h>
#ifdef CONFIG_SYSTEM_REBOOT_REASON
#include <tinyara/reboot_reason.h>
#endif

/* These are example limits, not limits of the public ioctl API. */
#define HM_EXAMPLE_MAX_MS 60000
#define HM_EXAMPLE_MAX_COUNT 100000
#define HM_EXAMPLE_MAX_ROUNDS 1000

struct hm_worker_s {
	int fd;
	sem_t *gate;
	unsigned int timeout;
	bool stress;
	pid_t pid;
	int result;
};

struct hm_workers_s {
	sem_t gate;
	struct hm_worker_s worker[2];
};

static int hm_number(const char *text, unsigned int maximum, unsigned int *value)
{
	char *end;
	unsigned long parsed;

	if (*text < '0' || *text > '9') {
		return -1;
	}
	set_errno(0);
	parsed = strtoul(text, &end, 10);
	if (errno || *end || parsed > maximum) {
		return -1;
	}
	*value = (unsigned int)parsed;
	return 0;
}

static int hm_delay(unsigned int milliseconds)
{
	struct timespec delay;

	delay.tv_sec = milliseconds / 1000;
	delay.tv_nsec = (milliseconds % 1000) * 1000000L;
	while (nanosleep(&delay, &delay) < 0) {
		if (errno != EINTR) {
			return -1;
		}
	}
	return 0;
}

static void *hm_worker(void *arg)
{
	struct hm_worker_s *worker = arg;
	unsigned int i;
	int ret;

	worker->pid = getpid();
	worker->result = -1;
	do {
		ret = sem_wait(worker->gate);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		return NULL;
	}
	if (worker->stress) {
		for (i = 0; i < 32; i++) {
			if (ioctl(worker->fd, HMIOC_START, (unsigned long)worker->timeout) < 0 ||
				ioctl(worker->fd, HMIOC_KICK, 0UL) < 0 ||
				ioctl(worker->fd, HMIOC_STOP, 0UL) < 0) {
				return NULL;
			}
		}
	}
	worker->result = ioctl(worker->fd, HMIOC_START, (unsigned long)worker->timeout);
	/* Intentionally leave the final registration to thread-exit cleanup. */
	return NULL;
}

static int hm_threads(int fd, unsigned int timeout, unsigned int rounds, bool stress)
{
	struct hm_workers_s *group;
	struct hm_worker_s *workers;
	pthread_t threads[2];
	pthread_attr_t attr;
	unsigned int count = stress ? 2 : 1;
	unsigned int round;
	unsigned int created;
	unsigned int i;
	int ret = 0;
	int err;
#ifdef CONFIG_SMP
	cpu_set_t cpuset;
#endif

	group = calloc(1, sizeof(*group));
	if (!group) {
		return -1;
	}
	if (sem_init(&group->gate, 0, 0) < 0) {
		free(group);
		return -1;
	}
	workers = group->worker;
	for (round = 0; round < rounds && ret == 0; round++) {
		created = 0;
		for (i = 0; i < count; i++) {
			workers[i].fd = fd;
			workers[i].gate = &group->gate;
			workers[i].timeout = timeout;
			workers[i].stress = stress;
			err = pthread_attr_init(&attr);
			if (err != 0) {
				ret = -1;
				break;
			}
#ifdef CONFIG_SMP
			if (stress) {
				CPU_ZERO(&cpuset);
				CPU_SET(i, &cpuset);
				err = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
			}
#endif
			if (err == 0) {
				err = pthread_create(&threads[i], &attr, hm_worker, &workers[i]);
			}
			pthread_attr_destroy(&attr);
			if (err != 0) {
				printf("health_monitor: pthread setup failed: %d\n", err);
				ret = -1;
				break;
			}
			created++;
		}
		/* Release only after both CPUs' workers exist. Also release any
		 * successfully created worker when the later creation fails.
		 */
		for (i = 0; i < created; i++) {
			sem_post(&group->gate);
		}
		for (i = 0; i < created; i++) {
			err = pthread_join(threads[i], NULL);
			if (err != 0) {
				/* An unjoined thread may still use workers and the shared fd.
				 * Keep both alive; the caller must not close this fd either.
				 */
				printf("health_monitor: join failed: %d; resources retained\n", err);
				return -2;
			}
			if (workers[i].result < 0) {
				ret = -1;
			}
			printf("health_monitor: round=%u worker=%u pid=%d result=%d\n",
				round + 1, i, (int)workers[i].pid, workers[i].result);
		}
	}
	sem_destroy(&group->gate);
	free(group);
	if (ret == 0) {
		/* Surviving the old deadlines checks the final exit registrations. */
		ret = hm_delay(timeout + 100);
	}
	return ret;
}

static int hm_run(int fd, unsigned int timeout, unsigned int period, unsigned int count)
{
	unsigned int i;
	int ret = 0;

	if (ioctl(fd, HMIOC_START, (unsigned long)timeout) < 0) {
		return -1;
	}
	for (i = 0; i < count; i++) {
		if (hm_delay(period) < 0 || ioctl(fd, HMIOC_KICK, 0UL) < 0) {
			ret = -1;
			break;
		}
	}
	if (ioctl(fd, HMIOC_STOP, 0UL) < 0) {
		ret = -1;
	}
	return ret;
}

static int hm_expire(int fd, unsigned int timeout)
{
	sem_t blocked;
	int ret;

	if (sem_init(&blocked, 0, 0) < 0) {
		return -1;
	}
	printf("health_monitor: deliberate timeout; expect PANIC/reboot reason 62\n");
	fflush(stdout);
	ret = ioctl(fd, HMIOC_START, (unsigned long)timeout);
	if (ret == 0) {
		/* No thread posts this semaphore. Signals must not end the test. */
		do {
			ret = sem_wait(&blocked);
		} while (ret < 0 && errno == EINTR);
		ioctl(fd, HMIOC_STOP, 0UL);
	}
	sem_destroy(&blocked);
	return -1; /* Returning at all is not a successful timeout test. */
}

static void hm_usage(void)
{
	printf("health_monitor run <timeout_ms> <period_ms> <count>\n"
		   "health_monitor stop <timeout_ms>\n"
		   "health_monitor exit <timeout_ms> <rounds>\n"
		   "health_monitor smp <timeout_ms> <rounds>\n"
		   "health_monitor reason  (requires SYSTEM_REBOOT_REASON)\n"
		   "health_monitor expire <timeout_ms>  (deliberate PANIC/reset)\n"
		   "Example limits: 1..60000 ms, period < timeout, count 1..100000, rounds 1..1000\n");
}

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int health_monitor_main(int argc, char *argv[])
#endif
{
	unsigned int timeout;
	unsigned int period = 0;
	unsigned int count = 0;
	bool run;
	bool stop;
	bool threads;
	bool stress;
	bool expire;
	int fd;
	int ret;

#ifdef CONFIG_SYSTEM_REBOOT_REASON
	if (argc == 2 && strcmp(argv[1], "reason") == 0) {
		ret = READ_REBOOT_REASON();
		printf("health_monitor: previous reboot reason=%d\n", ret);
		return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}
#endif
	if (argc < 3 || hm_number(argv[2], HM_EXAMPLE_MAX_MS, &timeout) < 0 || timeout == 0) {
		hm_usage();
		return EXIT_FAILURE;
	}
	run = strcmp(argv[1], "run") == 0;
	stop = strcmp(argv[1], "stop") == 0;
	stress = strcmp(argv[1], "smp") == 0;
	threads = stress || strcmp(argv[1], "exit") == 0;
	expire = strcmp(argv[1], "expire") == 0;
	if ((!run && !stop && !threads && !expire) ||
		(run && (argc != 5 || hm_number(argv[3], timeout - 1, &period) < 0 || period == 0 ||
				 hm_number(argv[4], HM_EXAMPLE_MAX_COUNT, &count) < 0 || count == 0)) ||
		(threads && (argc != 4 || hm_number(argv[3], HM_EXAMPLE_MAX_ROUNDS, &count) < 0 || count == 0)) ||
		((stop || expire) && argc != 3)) {
		hm_usage();
		return EXIT_FAILURE;
	}
#if !defined(CONFIG_SMP) || CONFIG_SMP_NCPUS < 2
	if (stress) {
		printf("health_monitor: smp requires at least two CPUs\n");
		return EXIT_FAILURE;
	}
#endif
	fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	if (fd < 0) {
		printf("health_monitor: open failed: %d\n", errno);
		return EXIT_FAILURE;
	}
	if (run) {
		ret = hm_run(fd, timeout, period, count);
	} else if (threads) {
		ret = hm_threads(fd, timeout, count, stress);
	} else if (stop) {
		ret = ioctl(fd, HMIOC_START, (unsigned long)timeout);
		if (ret == 0) {
			ret = ioctl(fd, HMIOC_STOP, 0UL);
		}
		if (ret == 0) {
			ret = hm_delay(timeout + 100);
		}
	} else {
		ret = hm_expire(fd, timeout);
	}
	if (ret != -2) {
		close(fd);
	}
	printf("health_monitor: %s %s\n", argv[1], ret == 0 ? "completed" : "FAILED");
	return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
