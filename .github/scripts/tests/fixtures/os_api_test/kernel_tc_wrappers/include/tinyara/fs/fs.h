#ifndef __TASK16_TINYARA_FS_H
#define __TASK16_TINYARA_FS_H

int task16_close(int fd);
int task16_ioctl(int fd, unsigned long request, ...);

#endif
