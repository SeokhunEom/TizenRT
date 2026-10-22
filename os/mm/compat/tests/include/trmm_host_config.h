/* SPDX-License-Identifier: Apache-2.0 */
#define CONFIG_BUILD_FLAT 1
#define CONFIG_MM_REGIONS 3
#define CONFIG_DEBUG_MM 1
#define CONFIG_DEBUG_ASSERTIONS 1
#define FAR
#define CODE
#define inline_function
#define UNUSED(a) ((void)(a))
#define ASSERT(c) assert(c)
#define DEBUGASSERT(c) assert(c)
#define DEBUGVERIFY(c) assert((c) >= 0)

#define DEBUGPANIC() assert(0)

#define PANIC() assert(0)
