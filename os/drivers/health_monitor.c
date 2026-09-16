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

#include <errno.h>
#include <limits.h>
#include <tinyara/fs/fs.h>
#include <tinyara/health_monitor.h>

#include "health_monitor/health_monitor.h"

/* The VFS requires read/write callbacks to accept open(O_RDWR). This is
 * an ioctl-only device: these callbacks reject data I/O without accessing
 * the buffer or changing registration. They do not allocate or wait.
 */

static ssize_t health_monitor_read(FAR struct file *filep, FAR char *buffer, size_t len)
{
	(void)filep;
	(void)buffer;
	(void)len;
	return -ENOSYS;
}

static ssize_t health_monitor_write(FAR struct file *filep, FAR const char *buffer, size_t len)
{
	(void)filep;
	(void)buffer;
	(void)len;
	return -ENOSYS;
}

/* Dispatch in the calling thread's context. The fd carries no owner or
 * private monitoring state, so sharing it never changes the target task.
 * START takes milliseconds by value, not a user pointer. KICK/STOP ignore
 * arg. Return kernel-style negative errno values; VFS translates them to
 * the application's -1/errno convention. The registry owns all locking.
 */

static int health_monitor_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
	(void)filep;

	switch (cmd) {
	case HMIOC_START:
#if ULONG_MAX > UINT32_MAX
		/* Do not silently truncate an argument on wider-word builds. */
		if (arg > UINT32_MAX) {
			return -EINVAL;
		}
#endif
		return health_monitor_start((uint32_t)arg);

	case HMIOC_KICK:
		health_monitor_kick();
		return OK;

	case HMIOC_STOP:
		return health_monitor_stop();

	default:
		return -ENOTTY;
	}
}

/* No open/close hooks or per-file state: open does not START and close
 * does not STOP. Registration ends only at STOP or task cleanup.
 */

static const struct file_operations g_health_monitor_fops = {
	.read = health_monitor_read,
	.write = health_monitor_write,
	.ioctl = health_monitor_ioctl,
};

/* Called once during board startup, after VFS initialization. The static
 * operations table has no device-private allocation. register_driver()
 * performs the existing VFS inode allocation/locking here, not in ioctl.
 * Propagate its result so startup can report device registration failure.
 */

int health_monitor_register(void)
{
	return register_driver(HEALTH_MONITOR_DEVPATH, &g_health_monitor_fops, 0666, NULL);
}
