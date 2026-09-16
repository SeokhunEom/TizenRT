/* SPDX-License-Identifier: Apache-2.0 */
/* Real registry + driver + VFS, with host task/IRQ/inode/fd storage models.
 * This does not emulate the protected-build SVC or execute board startup.
 */

#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_NFILE_DESCRIPTORS 8
#define CONFIG_NSOCKET_DESCRIPTORS 0
#define CONFIG_DISABLE_MOUNTPOINT 1
#define CONFIG_DISABLE_POLL 1
#define CONFIG_LIBC_IOCTL_VARIADIC 1
#define DEBUGASSERT(condition) assert(condition)
#define set_errno(value) (errno = (value))
#define ERROR (-1)

/* Rename both entry points and matching fops fields consistently. Never
 * replace the host C library's open/close used by sanitizers.
 */
#define open hm_test_open
#define close hm_test_close

#include "../health_monitor.c"
#include "../../../drivers/health_monitor.c"
#include "../../../fs/vfs/fs_ioctl.c"
#include "../../../fs/vfs/fs_getfilep.c"
#include "../../../fs/inode/fs_fileclose.c"

/* vopen's existing ap parameter is unused without FILE_MODE/mountpoints. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "../../../fs/vfs/fs_open.c"
#pragma GCC diagnostic pop
#include "../../../fs/vfs/fs_close.c"

static _Thread_local struct tcb_s *g_current;
static _Thread_local unsigned int g_irq_masked;
static _Thread_local unsigned int g_lock_held;
static struct tcb_s g_tasks[2];
static uint32_t g_now;
static struct inode g_inode;
static struct filelist g_files;
static bool g_registered;
static int g_register_error;

irqstate_t irqsave(void)
{
	unsigned int previous = g_irq_masked;
	g_irq_masked = 1;
	return previous;
}

void irqrestore(irqstate_t flags)
{
	assert(!g_lock_held);
	g_irq_masked = flags;
}

void spin_lock_wo_note(volatile spinlock_t *lock)
{
	assert(g_irq_masked && !g_lock_held);
	while (__atomic_exchange_n(lock, SP_LOCKED, __ATOMIC_ACQUIRE) == SP_LOCKED) {
	}
	g_lock_held = 1;
}

void spin_unlock_wo_note(volatile spinlock_t *lock)
{
	assert(g_irq_masked && g_lock_held);
	g_lock_held = 0;
	__atomic_store_n(lock, SP_UNLOCKED, __ATOMIC_RELEASE);
}

struct tcb_s *this_task(void)
{
	assert(g_current != NULL);
	return g_current;
}

clock_t clock_systimer(void)
{
	assert(g_irq_masked);
	return g_now;
}

/* Only registration storage and fd/inode lifetime are modeled here.
 * Actual open checks, ioctl dispatch/errno translation and file_close
 * driver-hook handling come from the production VFS sources above.
 */

int register_driver(const char *path, const struct file_operations *ops, mode_t mode, void *priv)
{
	assert(strcmp(path, HEALTH_MONITOR_DEVPATH) == 0);
	assert(mode == 0666 && priv == NULL);
	if (g_register_error) {
		return g_register_error;
	}
	if (g_registered) {
		return -EEXIST;
	}
	g_inode.u.i_ops = ops;
	g_registered = true;
	return OK;
}

struct inode *inode_find(const char *path, const char **relpath)
{
	(void)relpath;
	if (!g_registered || strcmp(path, HEALTH_MONITOR_DEVPATH) != 0) {
		return NULL;
	}
	g_inode.refs++;
	return &g_inode;
}

void inode_release(struct inode *inode)
{
	assert(inode == &g_inode && inode->refs > 0);
	inode->refs--;
}

struct filelist *sched_getfiles(void)
{
	return &g_files; /* Both test threads belong to one fd-sharing group. */
}

int files_allocate(struct inode *inode, int oflags, off_t pos, int minfd)
{
	int fd;
	for (fd = minfd; fd < CONFIG_NFILE_DESCRIPTORS; fd++) {
		if (g_files.fl_files[fd].f_inode == NULL) {
			g_files.fl_files[fd] = (struct file){oflags, pos, inode, NULL};
			return fd;
		}
	}
	return -1;
}

void files_release(int fd)
{
	memset(&g_files.fl_files[fd], 0, sizeof(struct file));
}

int files_close(int fd)
{
	if (g_files.fl_files[fd].f_inode == NULL) {
		return -EBADF;
	}
	return file_close(&g_files.fl_files[fd]);
}

static void expect_error(int fd, int cmd, unsigned long arg, int expected)
{
	errno = 0;
	assert(fs_ioctl(fd, cmd, arg) == -1);
	assert(errno == expected);
}

static void test_driver(void)
{
	int fd;
	int second;
	unsigned int i;
	for (i = 0; i < 2; i++) {
		g_tasks[i].pid = i + 2;
		health_monitor_task_init(&g_tasks[i]);
	}
	g_current = &g_tasks[0];
	g_register_error = -ENOMEM;
	assert(health_monitor_register() == -ENOMEM);
	assert(hm_test_open(HEALTH_MONITOR_DEVPATH, O_RDWR) == -1 && errno == ENOENT);
	g_register_error = 0;
	assert(health_monitor_register() == OK);
	assert(health_monitor_register() == -EEXIST);
	assert(g_inode.u.i_ops->open == NULL && g_inode.u.i_ops->close == NULL);
	fd = hm_test_open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	assert(fd >= 0 && g_health_count == 0);
	assert(g_files.fl_files[fd].f_priv == NULL);
	assert(g_inode.u.i_ops->read(&g_files.fl_files[fd], NULL, 0) == -ENOSYS);
	assert(g_inode.u.i_ops->write(&g_files.fl_files[fd], NULL, 0) == -ENOSYS);
	expect_error(-1, HMIOC_START, 10UL, EBADF);
	expect_error(CONFIG_NFILE_DESCRIPTORS, HMIOC_START, 10UL, EBADF);
	expect_error(fd, _HMIOC(99), 0UL, ENOTTY);
	expect_error(fd, HMIOC_START, 0UL, EINVAL);
	expect_error(fd, HMIOC_START, UINT32_MAX, EINVAL);
#if ULONG_MAX > UINT32_MAX
	expect_error(fd, HMIOC_START, (unsigned long)UINT32_MAX + 2UL, EINVAL);
#endif
	assert(fs_ioctl(fd, HMIOC_KICK, 0UL) == OK && g_health_count == 0);
	expect_error(fd, HMIOC_STOP, 0UL, ENOENT);
	assert(fs_ioctl(fd, HMIOC_START, 100UL) == OK);
	expect_error(fd, HMIOC_START, 200UL, EEXIST);
	assert(health_monitor_state(&g_tasks[0])->deadline == 100);
	g_now = 150; /* Even an elapsed deadline can be kicked before inspection. */
	assert(fs_ioctl(fd, HMIOC_KICK, 0UL) == OK);
	assert(health_monitor_state(&g_tasks[0])->deadline == 250);
	assert(g_health_heap[0].check_at == 100);

	/* A shared fd acts on the caller, not the thread which opened it. */
	g_current = &g_tasks[1];
	assert(fs_ioctl(fd, HMIOC_KICK, 0UL) == OK && g_health_count == 1);
	expect_error(fd, HMIOC_STOP, 0UL, ENOENT);
	assert(fs_ioctl(fd, HMIOC_START, 300UL) == OK && g_health_count == 2);
	g_now = 200;
	assert(fs_ioctl(fd, HMIOC_KICK, 0UL) == OK);
	assert(health_monitor_state(&g_tasks[0])->deadline == 250);
	assert(health_monitor_state(&g_tasks[1])->deadline == 500);
	assert(fs_ioctl(fd, HMIOC_STOP, 0UL) == OK && g_health_count == 1);
	assert(health_monitor_state(&g_tasks[0])->timeout == 100);
	assert(fs_ioctl(fd, HMIOC_START, 300UL) == OK);

	/* Closing the only fd leaves BOTH registrations intact. Reopening
	 * still targets the caller, independent of which fd registered it.
	 */
	assert(hm_test_close(fd) == OK && g_health_count == 2);
	expect_error(fd, HMIOC_KICK, 0UL, EBADF);
	second = hm_test_open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	assert(second >= 0);
	expect_error(second, HMIOC_START, 300UL, EEXIST);
	assert(fs_ioctl(second, HMIOC_STOP, 0UL) == OK);
	health_monitor_cleanup(&g_tasks[0]);
	assert(g_health_count == 0);
	assert(hm_test_close(second) == OK && g_inode.refs == 0);
	assert(!g_irq_masked && !g_lock_held);
}

#ifdef CONFIG_SMP
static pthread_barrier_t g_barrier;
struct worker_s {
	int fd;
	struct tcb_s *tcb;
};

static void *ioctl_worker(void *arg)
{
	struct worker_s *worker = arg;
	unsigned int i;
	int ret;
	g_current = worker->tcb;
	ret = pthread_barrier_wait(&g_barrier);
	assert(ret == 0 || ret == PTHREAD_BARRIER_SERIAL_THREAD);
	for (i = 0; i < 10000; i++) {
		assert(fs_ioctl(worker->fd, HMIOC_START, 100UL) == OK);
		assert(fs_ioctl(worker->fd, HMIOC_KICK, 0UL) == OK);
		assert(fs_ioctl(worker->fd, HMIOC_STOP, 0UL) == OK);
	}
	assert(!g_irq_masked && !g_lock_held);
	return NULL;
}

static void test_shared_fd_concurrent(void)
{
	pthread_t threads[2];
	int fd = hm_test_open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	struct worker_s workers[2] = {{fd, &g_tasks[0]}, {fd, &g_tasks[1]}};
	assert(fd >= 0);
	assert(pthread_barrier_init(&g_barrier, NULL, 2) == 0);
	assert(pthread_create(&threads[0], NULL, ioctl_worker, &workers[0]) == 0);
	assert(pthread_create(&threads[1], NULL, ioctl_worker, &workers[1]) == 0);
	assert(pthread_join(threads[0], NULL) == 0);
	assert(pthread_join(threads[1], NULL) == 0);
	assert(pthread_barrier_destroy(&g_barrier) == 0);
	assert(g_health_count == 0);
	assert(hm_test_close(fd) == OK && g_inode.refs == 0);
}
#endif

int main(void)
{
	test_driver();
#ifdef CONFIG_SMP
	test_shared_fd_concurrent();
	puts("PASS: SMP driver/VFS, errno, close/reopen, concurrent shared-fd callers");
#else
	puts("PASS: UP driver/VFS, errno, close/reopen, shared-fd caller identity");
#endif
	return 0;
}
