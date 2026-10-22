/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_NUTTX_MM_PROCFS_H
#define TIZENRT_NUTTX_MM_PROCFS_H
/* NuttX procfs meminfo registration is not the TizenRT procfs ABI. */
#ifndef CONFIG_FS_PROCFS_EXCLUDE_MEMINFO
#define CONFIG_FS_PROCFS_EXCLUDE_MEMINFO 1
#endif
#endif
