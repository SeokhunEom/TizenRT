/* SPDX-License-Identifier: Apache-2.0 */
#include <tinyara/config.h>
#include <tinyara/os_api_test_drv.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int nuttx_mm_lifecycle_main(int argc, char **argv)
{
	int fd;
	int ret;
	(void)argc;
	(void)argv;
	fd = open(OS_API_TEST_DRVPATH, O_RDWR);
	if (fd < 0) {
		printf("NXMM LIFE RESULT FAIL reason=open\n");
		return 1;
	}
	ret = ioctl(fd, TESTIOC_MM_LIFECYCLE_RUN, 0);
	close(fd);
	printf("NXMM LIFE RESULT %s cycles=3\n", ret == 0 ? "PASS" : "FAIL");
	return ret == 0 ? 0 : 1;
}
