/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_TINYARA_LOG_DICTIONARY_H
#define __INCLUDE_TINYARA_LOG_DICTIONARY_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <tinyara/compiler.h>

#include <stdint.h>
#include <stdio.h>
#include <syslog.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LOGDICT_DOMAIN_KERNEL  1
#define LOGDICT_DOMAIN_ARCH    2
#define LOGDICT_DOMAIN_DRIVERS 3

#define LOGDICT_FLAG_NORMAL    0x00
#define LOGDICT_FLAG_LOWPUT    0x01
#define LOGDICT_FLAG_PRINTF    0x02

#define LOGDICT_SITE_MAGIC     0x47444c54
#define LOGDICT_VERSION        1

#define LOGDICT_ARG_END        0
#define LOGDICT_ARG_INT        1
#define LOGDICT_ARG_UINT       2
#define LOGDICT_ARG_LONG       3
#define LOGDICT_ARG_ULONG      4
#define LOGDICT_ARG_LONGLONG   5
#define LOGDICT_ARG_ULONGLONG  6
#define LOGDICT_ARG_PTR        7
#define LOGDICT_ARG_STRING     8
#define LOGDICT_ARG_CHAR       9
#define LOGDICT_ARG_SIZE       10
#define LOGDICT_ARG_SSIZE      11
#define LOGDICT_ARG_INTMAX     12
#define LOGDICT_ARG_UINTMAX    13
#define LOGDICT_ARG_STRPREC    14

#define LOGDICT_STRINGIFY_INTERNAL(x) #x
#define LOGDICT_STRINGIFY(x) LOGDICT_STRINGIFY_INTERNAL(x)
#define LOGDICT_CONCAT_INTERNAL(a, b) a##b
#define LOGDICT_CONCAT(a, b) LOGDICT_CONCAT_INTERNAL(a, b)

#define LOGDICT_SECTION(name) __attribute__((section(name), used, aligned(4)))

/****************************************************************************
 * Public Type Declarations
 ****************************************************************************/

struct logdict_site_s {
	uint32_t magic;
	uint32_t id;
	uint16_t domain;
	uint8_t priority;
	uint8_t flags;
	uint32_t argdesc_lo;
	uint32_t argdesc_hi;
	uint32_t line;
	/* Keep the metadata section live until the host finalizer removes it. */
	const void *meta;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(CONFIG_LOG_DICTIONARY)
int logdict_emit_site(const struct logdict_site_s *site, ...);
#endif

#if defined(__cplusplus)
}
#endif

/****************************************************************************
 * Public Inline-like Macros
 ****************************************************************************/

#if defined(CONFIG_LOG_DICTIONARY) && defined(LOGDICT_DOMAIN) && \
	defined(CONFIG_CPP_HAVE_VARARGS)

#define LOGDICT_EMIT_INTERNAL(unique, flags, domain, priority, format, ...) \
	do { \
		static const char LOGDICT_CONCAT(g_logdict_meta_, unique)[] \
			LOGDICT_SECTION(".logdict_meta") = \
			__FILE__ "\037" LOGDICT_STRINGIFY(__LINE__) "\037" format; \
		static const struct logdict_site_s LOGDICT_CONCAT(g_logdict_site_, unique) \
			LOGDICT_SECTION(".logdict_site") = { \
			LOGDICT_SITE_MAGIC, 0, (domain), (priority), (flags), \
			0, 0, __LINE__, \
			LOGDICT_CONCAT(g_logdict_meta_, unique) \
		}; \
		(void)LOGDICT_CONCAT(g_logdict_meta_, unique); \
		logdict_emit_site(&LOGDICT_CONCAT(g_logdict_site_, unique), \
				  ##__VA_ARGS__); \
	} while (0)

#define LOGDICT_EMIT(flags, domain, priority, format, ...) \
	LOGDICT_EMIT_INTERNAL(__COUNTER__, flags, domain, priority, format, \
			      ##__VA_ARGS__)

#define logdict_printf(format, ...) \
	LOGDICT_EMIT(LOGDICT_FLAG_PRINTF, LOGDICT_DOMAIN, LOG_INFO, format, \
		     ##__VA_ARGS__)

#else

#define logdict_printf printf

#endif

#endif /* __INCLUDE_TINYARA_LOG_DICTIONARY_H */
