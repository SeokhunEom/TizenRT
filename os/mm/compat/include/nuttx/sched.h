/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_NUTTX_MM_SCHED_H
#define TIZENRT_NUTTX_MM_SCHED_H
#include <sys/types.h>
#include <nuttx/arch.h>
pid_t trmm_gettid(void);
#define _SCHED_GETTID() trmm_gettid()
#define this_cpu() 0
#endif
