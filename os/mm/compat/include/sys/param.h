/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_NUTTX_MM_SYS_PARAM_H
#define TIZENRT_NUTTX_MM_SYS_PARAM_H
/* mm_realloc needs MIN only. Do not mix newlib's sys/types with TizenRT's. */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#endif
