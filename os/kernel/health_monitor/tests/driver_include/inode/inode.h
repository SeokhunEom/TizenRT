/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HEALTH_MONITOR_TEST_INODE_H
#define HEALTH_MONITOR_TEST_INODE_H
#include <tinyara/fs/fs.h>
#define INODE_IS_DRIVER(inode) ((inode) != NULL)
struct inode *inode_find(const char *, const char **);
void inode_release(struct inode *);
#endif
