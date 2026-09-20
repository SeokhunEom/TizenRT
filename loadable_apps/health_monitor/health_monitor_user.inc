/* Test-only protected/XIP app fixture. Uses public syscalls, never kernel TCBs. */
#include <tinyara/config.h>
#include <tinyara/health_monitor.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *hu_name;
static unsigned hu_checks;
#define HU_CHECK(e) do { hu_checks++; if (!(e)) { printf("HM_USER FAIL app=%s line=%d expr=%s errno=%d\n", hu_name, __LINE__, #e, errno); return -1; } } while (0)
static int hu_sleep(unsigned ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
	while (nanosleep(&t, &t) < 0) if (errno != EINTR) return -1;
	return 0;
}
static int hu_wait(sem_t *s)
{
	int ret;
	do { ret = sem_wait(s); } while (ret < 0 && errno == EINTR);
	return ret;
}
struct hu_actor {
	pthread_t tid;
	sem_t request, reply;
	int fd, op, rc, error;
	unsigned long arg;
	pid_t pid;
};
static void *hu_worker(void *value)
{
	struct hu_actor *a = value;
	a->pid = getpid();
	sem_post(&a->reply);
	for (;;) {
		if (hu_wait(&a->request) != 0) return (void *)1;
		if (a->op == -1) return NULL;
		if (a->op == -2) pthread_exit((void *)0x1234);
		a->rc = ioctl(a->fd, a->op, a->arg);
		a->error = errno;
		sem_post(&a->reply);
	}
}
static int hu_open(struct hu_actor *a, int fd)
{
	memset(a, 0, sizeof(*a)); a->fd = fd;
	HU_CHECK(sem_init(&a->request, 0, 0) == 0 && sem_init(&a->reply, 0, 0) == 0);
	HU_CHECK(pthread_create(&a->tid, NULL, hu_worker, a) == 0);
	HU_CHECK(hu_wait(&a->reply) == 0);
	return 0;
}
static int hu_call(struct hu_actor *a, int op, unsigned long arg)
{
	a->op = op; a->arg = arg;
	sem_post(&a->request);
	if (hu_wait(&a->reply) != 0) return -2;
	return a->rc;
}
static int hu_close(struct hu_actor *a, int mode)
{
	void *result;
	if (mode == 1) HU_CHECK(pthread_cancel(a->tid) == 0);
	else { a->op = mode == 2 ? -2 : -1; sem_post(&a->request); }
	HU_CHECK(pthread_join(a->tid, &result) == 0);
	HU_CHECK(result == (mode == 1 ? PTHREAD_CANCELED : mode == 2 ? (void *)0x1234 : NULL));
	HU_CHECK(sem_destroy(&a->request) == 0 && sem_destroy(&a->reply) == 0);
	return 0;
}
static int hu_api(void)
{
	int fd, reopened;
	char c = 0x55;
	unsigned i;
	struct hu_actor a[2];
	hu_checks = 0;
	fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	HU_CHECK(fd >= 0);
	HU_CHECK(ioctl(fd, HMIOC_KICK, 0UL) == 0);
	HU_CHECK(ioctl(fd, HMIOC_STOP, 0UL) == -1 && errno == ENOENT);
	HU_CHECK(ioctl(fd, HMIOC_START, 0UL) == -1 && errno == EINVAL);
	HU_CHECK(ioctl(fd, HMIOC_START + 32, 0UL) == -1 && errno == ENOTTY);
	HU_CHECK(ioctl(-1, HMIOC_KICK, 0UL) == -1 && errno == EBADF);
	HU_CHECK(read(fd, &c, 0) == -1 && errno == ENOSYS);
	HU_CHECK(read(fd, &c, 1) == -1 && errno == ENOSYS && c == 0x55);
	HU_CHECK(write(fd, &c, 0) == -1 && errno == ENOSYS);
	HU_CHECK(write(fd, &c, 1) == -1 && errno == ENOSYS);
	HU_CHECK(ioctl(fd, HMIOC_START, (unsigned long)INT32_MAX) == 0);
	HU_CHECK(ioctl(fd, HMIOC_START, 5000UL) == -1 && errno == EEXIST);
	HU_CHECK(ioctl(fd, HMIOC_STOP, 0UL) == 0);
	HU_CHECK(ioctl(fd, HMIOC_START, (unsigned long)INT32_MAX + 1UL) == -1 && errno == EINVAL);
	HU_CHECK(ioctl(fd, HMIOC_START, (unsigned long)UINT32_MAX) == -1 && errno == EINVAL);
	HU_CHECK(ioctl(fd, HMIOC_STOP, 0UL) == -1 && errno == ENOENT);
	HU_CHECK(ioctl(fd, HMIOC_START, 5000UL) == 0);
	HU_CHECK(close(fd) == 0);
	HU_CHECK(ioctl(fd, HMIOC_KICK, 0UL) == -1 && errno == EBADF);
	reopened = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	HU_CHECK(reopened >= 0);
	HU_CHECK(ioctl(reopened, HMIOC_START, 5000UL) == -1 && errno == EEXIST);
	HU_CHECK(ioctl(reopened, HMIOC_KICK, 0UL) == 0 && ioctl(reopened, HMIOC_STOP, 0UL) == 0);
	HU_CHECK(close(reopened) == 0);
	fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR); HU_CHECK(fd >= 0);
	HU_CHECK(hu_open(&a[0], fd) == 0 && hu_open(&a[1], fd) == 0);
	HU_CHECK(hu_call(&a[0], HMIOC_START, 5000) == 0 && hu_call(&a[1], HMIOC_START, 6000) == 0);
	HU_CHECK(ioctl(fd, HMIOC_STOP, 0UL) == -1 && errno == ENOENT);
	HU_CHECK(hu_call(&a[0], HMIOC_STOP, 0) == 0);
	HU_CHECK(hu_call(&a[1], HMIOC_START, 7000) == -1 && a[1].error == EEXIST);
	HU_CHECK(hu_call(&a[1], HMIOC_KICK, 0) == 0 && hu_call(&a[1], HMIOC_STOP, 0) == 0);
	for (i = 0; i < 1000; i++) {
		HU_CHECK(hu_call(&a[i & 1], HMIOC_START, 5000) == 0);
		HU_CHECK(hu_call(&a[i & 1], HMIOC_KICK, 0) == 0);
		HU_CHECK(hu_call(&a[i & 1], HMIOC_STOP, 0) == 0);
	}
	HU_CHECK(hu_call(&a[0], HMIOC_START, 500) == 0 && hu_call(&a[1], HMIOC_START, 500) == 0);
	HU_CHECK(hu_close(&a[0], 1) == 0 && hu_close(&a[1], 2) == 0);
	HU_CHECK(hu_sleep(650) == 0);
	HU_CHECK(hu_open(&a[0], fd) == 0 && hu_call(&a[0], HMIOC_START, 100) == 0);
	HU_CHECK(hu_close(&a[0], 0) == 0 && hu_sleep(150) == 0);
	HU_CHECK(close(fd) == 0);
	printf("HM_USER PASS app=%s checks=%u pid=%d cycles=1000 shared_fd=1 cancel=1 exit=1 return=1\n", hu_name, hu_checks, getpid());
	return 0;
}
static struct hu_actor hu_armed[4];
static __attribute__((noinline)) void hu_fault_udf(void)
{
	__asm__ __volatile__("udf #0");
}

static __attribute__((noinline)) void hu_fault_mpu(void)
{
	/* Read kernel RAM from an unprivileged application; never corrupt it. */
	volatile uint32_t value = *(volatile uint32_t *)0x80000000U;
	(void)value;
}

static int hu_fault(bool mpu, bool registered)
{
	unsigned control;
	int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	HU_CHECK(fd >= 0);
	if (registered) HU_CHECK(ioctl(fd, HMIOC_START, 60000UL) == 0);
	__asm__ __volatile__("mrs %0, control" : "=r" (control));
	HU_CHECK((control & 1) != 0);
	printf("HM_USER CPU_FAULT app=%s pid=%d kind=%s control=%u registered=%u timeout_ms=%u\n",
		hu_name, getpid(), mpu ? "mpu" : "udf", control, registered, registered ? 60000 : 0);
	if (mpu) hu_fault_mpu();
	else hu_fault_udf();
	printf("HM_USER FAIL app=%s CPU fault unexpectedly returned\n", hu_name);
	return -1;
}

static int hu_arm(void)
{
	unsigned i;
	int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	HU_CHECK(fd >= 0);
	for (i = 0; i < 4; i++) {
		HU_CHECK(hu_open(&hu_armed[i], fd) == 0);
		HU_CHECK(hu_call(&hu_armed[i], HMIOC_START, 5000) == 0);
		printf("HM_USER ARMED_CHILD app=%s pid=%d\n", hu_name, hu_armed[i].pid);
	}
	HU_CHECK(ioctl(fd, HMIOC_START, 5000UL) == 0);
	printf("HM_USER ARMED app=%s pid=%d children=4 timeout_ms=5000\n", hu_name, getpid());
	return 0;
}
static int hm_user_main(const char *name)
{
	char path[48], command[24];
	unsigned control;
	int fd, count;
	hu_name = name;
	__asm__ __volatile__("mrs %0, control" : "=r" (control));
	printf("HM_USER BOOT app=%s pid=%d control=%u unprivileged=%u\n", name, getpid(), control, control & 1);
	HU_CHECK((control & 1) != 0);
	HU_CHECK(binary_manager_notify_binary_started() == 0);
	snprintf(path, sizeof(path), "/tmp/hm-%s-command", name);
	if (hu_api() != 0) return 1;
	printf("HM_USER READY app=%s pid=%d\n", name, getpid());
	for (;;) {
		fd = open(path, O_RDONLY);
		if (fd >= 0) {
			count = read(fd, command, sizeof(command) - 1);
			close(fd); unlink(path);
			if (count > 0) {
				command[count] = 0;
				if (!strcmp(command, "api")) { if (hu_api() != 0) return 1; }
				else if (!strcmp(command, "arm")) { if (hu_arm() != 0) return 1; }
				else if (!strcmp(command, "fault-udf") || !strcmp(command, "fault-mpu") ||
					!strcmp(command, "fault-udf-empty") || !strcmp(command, "fault-mpu-empty")) {
					return hu_fault(strstr(command, "mpu") != NULL, strstr(command, "empty") == NULL);
				}
				else if (!strcmp(command, "expire")) {
					fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR); HU_CHECK(fd >= 0);
					HU_CHECK(ioctl(fd, HMIOC_START, 1000UL) == 0);
					printf("HM_USER EXPIRE app=%s pid=%d timeout_ms=1000\n", name, getpid());
					for (;;) hu_sleep(100);
				} else if ((!strcmp(command, "return") || !strcmp(command, "return-empty"))) {
					fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR); HU_CHECK(fd >= 0);
					if (!strcmp(command, "return")) HU_CHECK(ioctl(fd, HMIOC_START, 500UL) == 0);
					printf("HM_USER RETURN app=%s pid=%d registered=%d\n", name, getpid(), !strcmp(command, "return"));
					return 0;
				} else { printf("HM_USER FAIL app=%s unknown_command=%s\n", name, command); return 1; }
			}
		}
		hu_sleep(50);
	}
}
