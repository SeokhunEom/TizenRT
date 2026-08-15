/****************************************************************************
 * apps/examples/testcase/ostest/ostest_unsupported.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <tinyara/config.h>

#include <stdio.h>

#include "ostest.h"

static void ostest_skip(FAR const char *name, FAR const char *reason)
{
  printf("ostest: SKIP %s: %s\n", name, reason);
}

int getopt_test(void)
{
  ostest_skip("getopt", "TizenRT libc does not provide getopt_long APIs");
  return 0;
}

int memmem_test(void)
{
  ostest_skip("memmem", "TizenRT libc does not provide memmem()");
  return 0;
}

#ifndef CONFIG_STDIO_DISABLE_BUFFERING
int setvbuf_test(void)
{
  ostest_skip("setvbuf",
              "TizenRT lib_fwrite loops on an unbuffered stream");
  return 0;
}
#endif

#ifndef CONFIG_DISABLE_ALL_SIGNALS
void sigprocmask_test(void)
{
  ostest_skip("sigprocmask", "required POSIX signal numbers are unavailable");
}
#endif

#ifndef CONFIG_DISABLE_PTHREAD
void timedmutex_test(void)
{
  ostest_skip("timedmutex", "TizenRT does not provide pthread_mutex_timedlock()");
}

void timedwait_test(void)
{
  ostest_skip("timedwait", "TizenRT does not provide gettid()");
}

#ifdef CONFIG_PRIORITY_INHERITANCE
void priority_inheritance(void)
{
  ostest_skip("prioinherit", "TizenRT does not provide gettid()");
}
#endif
#endif

#ifdef CONFIG_BUILD_FLAT
void spinlock_test(void)
{
  ostest_skip("spinlock", "TizenRT has no atomic/seqlock public API");
}

void wdog_test(void)
{
  ostest_skip("wdog", "NuttX watchdog test uses an incompatible internal API");
}

#if !defined(CONFIG_DISABLE_PTHREAD) && defined(CONFIG_SCHED_WORKQUEUE)
void wqueue_test(void)
{
  ostest_skip("wqueue", "TizenRT has no dynamic workqueue creation API");
}
#endif
#endif

#if !defined(CONFIG_DISABLE_PTHREAD) && CONFIG_RR_INTERVAL > 0
void rr_test(void)
{
  ostest_skip("roundrobin", "TizenRT has no NuttX atomic public API");
}
#endif
