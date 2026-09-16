/* SPDX-License-Identifier: Apache-2.0 */
/* Minimal host VFS storage model. Production VFS operations are compiled. */
#ifndef HEALTH_MONITOR_TEST_FS_H
#define HEALTH_MONITOR_TEST_FS_H
#include <tinyara/config.h>
#include <tinyara/compiler.h>
#include <sys/types.h>
#include <stddef.h>
struct file;
struct inode;
struct file_operations {
	int (*open)(struct file *);
	int (*close)(struct file *);
	ssize_t (*read)(struct file *, char *, size_t);
	ssize_t (*write)(struct file *, const char *, size_t);
	off_t (*seek)(struct file *, off_t, int);
	int (*ioctl)(struct file *, int, unsigned long);
	int (*unlink)(struct inode *);
};
struct inode {
	union {
		const struct file_operations *i_ops;
	} u;
	int refs;
};
struct file {
	int f_oflags;
	off_t f_pos;
	struct inode *f_inode;
	void *f_priv;
};
struct filelist {
	struct file fl_files[CONFIG_NFILE_DESCRIPTORS];
};
int register_driver(const char *, const struct file_operations *, mode_t, void *);
int fs_getfilep(int, struct file **);
struct filelist *sched_getfiles(void);
int files_allocate(struct inode *, int, off_t, int);
void files_release(int);
int files_close(int);
int file_close(struct file *);
int fs_ioctl(int, int, unsigned long);
#endif
