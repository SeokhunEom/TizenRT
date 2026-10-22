/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_NUTTX_MM_MALLOC_H
#define TIZENRT_NUTTX_MM_MALLOC_H
/* stdlib is included before the private mallinfo token mapping in symbol_map.h. */
#include <sys/types.h>
#define PID_MM_ORPHAN ((pid_t)-6)
#define PID_MM_BIGGEST ((pid_t)-5)
#define PID_MM_FREE ((pid_t)-4)
#define PID_MM_ALLOC ((pid_t)-3)
#define PID_MM_LEAK ((pid_t)-2)
#define PID_MM_MEMPOOL ((pid_t)-1)
struct mallinfo {
  int arena;
  int ordblks;
  int aordblks;
  int mxordblk;
  int uordblks;
  int fordblks;
  int usmblks;
};
struct malltask { pid_t pid; };
struct mallinfo_task { int aordblks; int uordblks; };
#endif
