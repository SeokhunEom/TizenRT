#ifndef __MLC_TASK6_NATIVE_CONFIG_H
#define __MLC_TASK6_NATIVE_CONFIG_H

#include <assert.h>
#include <errno.h>

#define FAR
#define OK 0
#define ERROR (-1)
#ifndef MLC_TASK6_USER_PASS
#define __KERNEL__ 1
#endif

#define CONFIG_PRIORITY_INHERITANCE 1
#define CONFIG_SEMAPHORE_HISTORY 1
#define CONFIG_SEM_PREALLOCHOLDERS 0
#define CONFIG_DISABLE_SIGNALS 1
#define CONFIG_SCHED_INSTRUMENTATION_CSECTION 1
#define CONFIG_TASK_NAME_SIZE 16
#define CONFIG_KMM_REGIONS 1
#define CONFIG_MEM_LEAK_CHECKER_MAX_ALLOC_COUNT 64
#define CONFIG_MEM_LEAK_CHECKER_HASH_TABLE_SIZE 31

typedef int (*main_t)(int argc, char **argv);
typedef void (*start_t)(void);

#define DEBUGASSERT(condition) ((void)0)
#define DEBUGVERIFY(condition) assert(condition)
#define set_errno(error) (errno = (error))
#define get_errno() errno

#ifndef EUCLEAN
#define EUCLEAN 117
#endif

#endif
