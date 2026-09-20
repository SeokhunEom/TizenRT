/* User-space control client; private operations execute in the test driver. */
#include <tinyara/config.h>
#include <tinyara/os_api_test_drv.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "health_monitor/tests/armv8m/control.h"

static int hm_request(int command)
{
	int fd = open(OS_API_TEST_DRVPATH, O_WRONLY);
	int ret;
	if (fd < 0) return -1;
	ret = ioctl(fd, command, 0UL);
	if (close(fd) < 0) return -1;
	return ret;
}

int hm_armv8_main(int argc, char **argv)
{
	char path[48], temp[48];
	int fd, ret;
	if (argc == 2 && !strcmp(argv[1], "status")) return hm_request(HM_TESTIOC_STATUS);
#ifdef CONFIG_BINMGR_RECOVERY
	if (argc == 2 && !strcmp(argv[1], "recover")) {
		printf("HM_CONTROL RECOVER request=1 real_binary_manager=1\n");
		if (hm_request(HM_TESTIOC_RECOVER) != 0) goto bad;
		printf("HM_CONTROL RECOVER dispatched=1\n");
		return 0;
	}
#endif
	if (argc == 4 && !strcmp(argv[1], "send") &&
		(!strcmp(argv[2], "app1") || !strcmp(argv[2], "app2"))) {
		if (strcmp(argv[3], "api") && strcmp(argv[3], "arm") &&
			strcmp(argv[3], "expire") && strcmp(argv[3], "return") && strcmp(argv[3], "return-empty") &&
			strcmp(argv[3], "fault-udf") && strcmp(argv[3], "fault-mpu") &&
			strcmp(argv[3], "fault-udf-empty") && strcmp(argv[3], "fault-mpu-empty")) goto bad;
		snprintf(path, sizeof(path), "/tmp/hm-%s-command", argv[2]);
		snprintf(temp, sizeof(temp), "/tmp/hm-%s-staging", argv[2]);
		fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0) goto bad;
		ret = write(fd, argv[3], strlen(argv[3]));
		if (close(fd) != 0 || ret != strlen(argv[3]) || rename(temp, path) != 0) goto bad;
		printf("HM_CONTROL SENT app=%s command=%s\n", argv[2], argv[3]);
		return 0;
	}
bad:
	printf("HM_CONTROL FAIL command errno=%d\n", errno);
	return 1;
}
