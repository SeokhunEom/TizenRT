/* SPDX-License-Identifier: Apache-2.0 */
/* Add to a test app and call from the thread to be monitored. This is not
 * built into the product automatically and does not test timeout/reset.
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <tinyara/health_monitor.h>

int health_monitor_smoke(void)
{
	int fd;
	int ret = 0;

	fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
	if (fd < 0) {
		return -errno;
	}

	/* The variadic ioctl wrapper expects unsigned long, including zero. */
	if (ioctl(fd, HMIOC_START, 1000UL) < 0) {
		ret = -errno;
		goto out;
	}
	if (ioctl(fd, HMIOC_KICK, 0UL) < 0) {
		ret = -errno;
	}

	/* STOP belongs to this thread. Closing the fd alone does not stop it. */
	if (ioctl(fd, HMIOC_STOP, 0UL) < 0 && ret == 0) {
		ret = -errno;
	}

out:
	if (close(fd) < 0 && ret == 0) {
		ret = -errno;
	}
	return ret;
}
