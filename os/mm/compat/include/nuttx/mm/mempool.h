/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_NUTTX_MM_MEMPOOL_H
#define TIZENRT_NUTTX_MM_MEMPOOL_H
#include <nuttx/config.h>
#if CONFIG_MM_HEAP_MEMPOOL_THRESHOLD >= 0
#error "Import mempool providers before enabling NuttX heap mempool"
#endif
/* No declarations required by the default heap when mempool is disabled. */
#endif
