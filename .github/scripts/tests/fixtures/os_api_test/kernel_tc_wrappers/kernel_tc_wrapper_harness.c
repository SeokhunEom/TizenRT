#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_common.h"
#include "include/tinyara/os_api_test_drv.h"

int total_pass;
int total_fail;

static int g_open_count;
static int g_close_count;
static int g_ioctl_count;
static int g_close_fd;
static int g_ioctl_fds[11];
static int g_ioctl_requests[11];
static int g_failed_request;

int task16_open(const char *path, int flags, ...)
{
	(void)flags;
	if (strcmp(path, OS_API_TEST_DRVPATH) != 0) {
		return -1;
	}

	g_open_count++;
	return 73;
}

int task16_close(int fd)
{
	g_close_count++;
	g_close_fd = fd;
	return 0;
}

int task16_ioctl(int fd, unsigned long request, ...)
{
	va_list args;

	va_start(args, request);
	(void)va_arg(args, int);
	va_end(args);
	g_ioctl_fds[g_ioctl_count] = fd;
	g_ioctl_requests[g_ioctl_count] = (int)request;
	g_ioctl_count++;
	return request == (unsigned long)g_failed_request ? -5 : OK;
}

int testcase_state_handler(tc_op_type_t type, const char *tc_name)
{
	printf("%s %s\n", type == TC_START ? "START" : "END", tc_name);
	return OK;
}

int tc_kernel_main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	int index;

	g_failed_request = argc == 2 ? atoi(argv[1]) : -1;
	(void)tc_kernel_main(0, NULL);
	printf("COUNTS open=%d close=%d close_fd=%d ioctl=%d pass=%d fail=%d\n",
		g_open_count, g_close_count, g_close_fd, g_ioctl_count, total_pass, total_fail);
	for (index = 0; index < g_ioctl_count; index++) {
		printf("IOCTL fd=%d request=%d\n", g_ioctl_fds[index], g_ioctl_requests[index]);
	}

	return 0;
}
