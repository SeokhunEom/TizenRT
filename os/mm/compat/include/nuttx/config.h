/* SPDX-License-Identifier: Apache-2.0 */
/* Private configuration projection for the imported NuttX allocator only. */
#ifndef TIZENRT_NUTTX_MM_CONFIG_H
#define TIZENRT_NUTTX_MM_CONFIG_H
#ifdef TRMM_HOST_TEST
#  include <trmm_host_config.h>
#else
#  include <tinyara/config.h>
#  include <tinyara/compiler.h>
#endif
/* Native arch headers also use inline_function as a suffix annotation.
 * Preserve TizenRT's attribute-only form there; upstream definitions remain
 * static and are still emitted correctly by GCC. */
#ifdef TRMM_HOST_TEST
#  undef inline_function
#  define inline_function inline __attribute__((always_inline))
#endif
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <assert.h>
#include <debug.h>
#if defined(CONFIG_SMP)
#  error "NuttX MM compatibility currently supports uniprocessor builds only"
#endif
#if defined(CONFIG_MM_KASAN) || defined(CONFIG_MM_TLSF) || defined(CONFIG_MM_HEAP_MEMPOOL)
#  error "NuttX MM compatibility supports only the default heap without KASAN/mempool"
#endif
#if defined(CONFIG_MM_DUMP_DETAILS_ON_FAILURE)
#  error "NuttX detailed memdump requires its execinfo/task metadata adapters"
#endif
#if defined(CONFIG_MM_DUMP_ON_FAILURE) || defined(CONFIG_MM_PANIC_ON_FAILURE)
#  error "NuttX internal heap failure policies are not registered; use TizenRT allocation-failure handling"
#endif
#if defined(CONFIG_MM_BACKTRACE) && CONFIG_MM_BACKTRACE != -1
#  error "NuttX backtrace metadata is not enabled; use TizenRT diagnostic metadata"
#endif
#ifndef CONFIG_MM_BACKTRACE
#  define CONFIG_MM_BACKTRACE -1
#endif
#ifndef CONFIG_MM_REGIONS
#  ifdef CONFIG_KMM_REGIONS
#    define CONFIG_MM_REGIONS CONFIG_KMM_REGIONS
#  else
#    define CONFIG_MM_REGIONS 1
#  endif
#endif
#ifndef CONFIG_SMP_NCPUS
#  define CONFIG_SMP_NCPUS 1
#endif
#if defined(CONFIG_MM_HEAP_MEMPOOL_THRESHOLD) && CONFIG_MM_HEAP_MEMPOOL_THRESHOLD >= 0
#  error "NuttX mempool provider has not been ported"
#endif
#ifndef CONFIG_MM_HEAP_MEMPOOL_THRESHOLD
#  define CONFIG_MM_HEAP_MEMPOOL_THRESHOLD -1
#endif
#ifndef CONFIG_MM_DEFAULT_ALIGNMENT
#  define CONFIG_MM_DEFAULT_ALIGNMENT 8
#endif
#ifndef CONFIG_MM_NODE_GUARDSIZE
#  define CONFIG_MM_NODE_GUARDSIZE 0
#endif
#ifndef CONFIG_MM_FREE_DELAYCOUNT_MAX
#  define CONFIG_MM_FREE_DELAYCOUNT_MAX 0
#endif
#ifndef CONFIG_MM_HEAP_BIGGEST_COUNT
#  define CONFIG_MM_HEAP_BIGGEST_COUNT 0
#endif
#if CONFIG_MM_NODE_GUARDSIZE != 0 || CONFIG_MM_FREE_DELAYCOUNT_MAX != 0
#  error "NuttX delayed-free tuning and node guards are not part of this profile"
#endif
#ifndef malloc_like1
#  define malloc_like1(a)
#  define malloc_like2(a,b)
#  define realloc_like(a)
#endif
#ifndef minfo
#  define minfo(...) mvdbg(__VA_ARGS__)
#endif
#ifndef mwarn
#  define mwarn(...) mdbg(__VA_ARGS__)
#endif
#ifndef flsl
#  define flsl(v) ((v) == 0 ? 0 : (int)(sizeof(unsigned long) * CHAR_BIT - __builtin_clzl((unsigned long)(v))))
#endif
#endif
