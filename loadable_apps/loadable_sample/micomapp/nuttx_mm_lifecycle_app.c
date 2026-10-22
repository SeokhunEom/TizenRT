/* SPDX-License-Identifier: Apache-2.0 */
#include <tinyara/config.h>
#include <tinyara/os_api_test_drv.h>
#include <binary_manager/binary_manager.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define APP_THREADS 2
#define APP_STEPS 256

static void *app_worker(void *arg)
{
	unsigned tag = (uintptr_t)arg;
	unsigned step;
	for (step = 0; step < APP_STEPS; step++) {
		size_t size = 17 + (step * 31 + tag) % 1024;
		unsigned char *mem = malloc(size);
		unsigned char *grown;
		size_t i;
		if (!mem) { return (void *)1; }
		memset(mem, (unsigned char)(tag + step), size);
		grown = realloc(mem, size + 113);
		if (!grown) { free(mem); return (void *)1; }
		mem = grown;
		for (i = 0; i < size; i++) {
			if (mem[i] != (unsigned char)(tag + step)) {
				free(mem);
				return (void *)1;
			}
		}
		free(mem);
	}
	return NULL;
}

static int app_late_report(unsigned int stage, unsigned int delay, size_t bytes)
{
	struct mm_lifecycle_app_report_s report = {0};
	unsigned char *mem;
	int fd;
	usleep(delay);
	report.stage = stage;
	mem = malloc(bytes);
	if (!mem) { report.status = 1; }
	else { memset(mem, 0x5a, bytes); }
	report.live = mem;
	report.live_bytes = mem ? bytes : 0;
	fd = open(OS_API_TEST_DRVPATH, O_RDWR);
	if (fd < 0) { report.status = 1; }
	else {
		/* The kernel checks prior task exit, group identity and heap residency. */
		if (ioctl(fd, TESTIOC_MM_LIFECYCLE_REPORT, (unsigned long)&report) != 0) { report.status = 1; }
		close(fd);
	}
	free(mem);
	return report.status;
}

static void *app_last_child(void *arg)
{
	int status;
	(void)arg;
	/* Keep the original task group alive after its main thread returns. */
	status = app_late_report(2, 200000, 521);
	printf("NXMM APP2 %s pid=%d after_main=1 final_child=1\n", status ? "FAIL" : "PASS", getpid());
	return NULL;
}

static int app_last_group(int argc, char *argv[])
{
	int status;
	(void)argc;
	(void)argv;
	/* This independently created group outlives the original whole group. */
	status = app_late_report(3, 500000, 733);
	printf("NXMM APP2 GROUP %s pid=%d after_main=1 after_child=1 separate_group=1\n",
		status ? "FAIL" : "PASS", getpid());
	return status;
}

int nuttx_mm_lifecycle_app(void)
{
	pthread_t threads[APP_THREADS];
	pthread_attr_t attr;
	struct mm_lifecycle_app_report_s report = {0};
	unsigned char *zero = NULL;
	void *aligned = NULL;
	void *live = NULL;
	int created = 0;
	int status = 0;
	int fd;
	int i;
	printf("NXMM APP2 BEGIN pid=%d threads=2 steps=256\n", getpid());
	if (binary_manager_notify_binary_started() != 0) { status = 1; }
	zero = calloc(11, 37);
	aligned = memalign(256, 379);
	if (!zero || !aligned || ((uintptr_t)aligned & 255)) { status = 1; }
	if (zero) {
		for (i = 0; i < 407; i++) { if (zero[i] != 0) { status = 1; } }
	}
	free(zero);
	free(aligned);
	if (pthread_attr_init(&attr) != 0) { status = 1; }
	else {
		if (pthread_attr_setstacksize(&attr, 4096) != 0) { status = 1; }
		else {
			for (i = 0; i < APP_THREADS; i++) {
				if (pthread_create(&threads[i], &attr, app_worker, (void *)(uintptr_t)(i + 1)) != 0) {
					status = 1;
					break;
				}
				created++;
			}
		}
		pthread_attr_destroy(&attr);
	}
	for (i = 0; i < created; i++) {
		void *result = (void *)1;
		if (pthread_join(threads[i], &result) != 0 || result != NULL) { status = 1; }
	}
	if (task_create("nxmm_late", 100, 4096, app_last_group, NULL) < 0) { status = 1; }
	if (pthread_attr_init(&attr) != 0) { status = 1; }
	else {
		pthread_t child;
		if (pthread_attr_setstacksize(&attr, 4096) != 0 ||
			pthread_create(&child, &attr, app_last_child, NULL) != 0) { status = 1; }
		else if (pthread_detach(child) != 0) { status = 1; }
		pthread_attr_destroy(&attr);
	}
	/* Deliberately retain one allocation until normal app exit. The kernel
	 * controller must prove the entire app arena and both heap locks vanish.
	 */
	live = malloc(257);
	if (!live) { status = 1; }
	else { memset(live, 0xa5, 257); }
	report.stage = 1;
	report.status = status;
	report.live_bytes = live ? 257 : 0;
	report.live = live;
	fd = open(OS_API_TEST_DRVPATH, O_RDWR);
	if (fd < 0) { status = 1; }
	else {
		if (ioctl(fd, TESTIOC_MM_LIFECYCLE_REPORT, (unsigned long)&report) != 0) { status = 1; }
		close(fd);
	}
	printf("NXMM APP2 MAIN %s pid=%d live_bytes=%u normal_exit=1\n",
		status ? "FAIL" : "PASS", getpid(), report.live_bytes);
	return status;
}
