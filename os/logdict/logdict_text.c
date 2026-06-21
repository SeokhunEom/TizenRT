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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#ifdef CONFIG_LOGM
#include <tinyara/logm.h>
#endif

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <syslog.h>

#include "logdict.h"
#ifndef CONFIG_LOGM
#include "syslog/syslog.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_LOG_DICTIONARY_TEXT_BUFSIZE
#define CONFIG_LOG_DICTIONARY_TEXT_BUFSIZE 256
#endif

#ifndef CONFIG_LOG_DICTIONARY_MAX_STRLEN
#define CONFIG_LOG_DICTIONARY_MAX_STRLEN 64
#endif

#define LOGDICT_MAX_ARGS 16

#ifdef CONFIG_SMALL_MEMORY
#if UINT16_MAX <= INT_MAX
#define LOGDICT_VA_ARG_SIZE(ap) ((unsigned long)va_arg(ap, int))
#else
#define LOGDICT_VA_ARG_SIZE(ap) ((unsigned long)va_arg(ap, unsigned int))
#endif
#define LOGDICT_VA_ARG_SSIZE(ap) ((long)va_arg(ap, int))
#else
#define LOGDICT_VA_ARG_SIZE(ap) ((unsigned long)va_arg(ap, size_t))
#define LOGDICT_VA_ARG_SSIZE(ap) ((long)va_arg(ap, ssize_t))
#endif

#ifdef __INT64_DEFINED
#define LOGDICT_INTMAX_FMT "%lld"
#define LOGDICT_UINTMAX_FMT "%llu"
#define LOGDICT_VA_ARG_INTMAX(ap) ((long long)va_arg(ap, intmax_t))
#define LOGDICT_VA_ARG_UINTMAX(ap) \
	((unsigned long long)va_arg(ap, uintmax_t))
#else
#define LOGDICT_INTMAX_FMT "%ld"
#define LOGDICT_UINTMAX_FMT "%lu"
#define LOGDICT_VA_ARG_INTMAX(ap) ((long)va_arg(ap, intmax_t))
#define LOGDICT_VA_ARG_UINTMAX(ap) ((unsigned long)va_arg(ap, uintmax_t))
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static size_t logdict_append(char *buf, size_t buflen, size_t pos,
			     const char *fmt, ...)
{
	va_list ap;
	int ret;

	if (pos >= buflen) {
		return pos;
	}

	va_start(ap, fmt);
	ret = vsnprintf(buf + pos, buflen - pos, fmt, ap);
	va_end(ap);

	if (ret < 0) {
		return pos;
	}

	pos += (size_t)ret;
	if (pos >= buflen) {
		pos = buflen - 1;
	}

	return pos;
}

static size_t logdict_append_escaped(char *buf, size_t buflen, size_t pos,
				     const char *str, size_t maxlen,
				     bool mark_truncated)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t count = 0;

	if (maxlen == 0) {
		return pos;
	}

	if (!str) {
		return logdict_append(buf, buflen, pos, "(null)");
	}

	while (count < maxlen && *str != '\0') {
		unsigned char ch = (unsigned char)*str++;

		if (ch <= 0x20 || ch == '%' || ch == '|' || ch >= 0x7f) {
			if (pos + 3 >= buflen) {
				return buflen > 0 ? buflen - 1 : 0;
			}

			buf[pos++] = '%';
			buf[pos++] = hex[ch >> 4];
			buf[pos++] = hex[ch & 0x0f];
		} else {
			if (pos + 1 >= buflen) {
				return buflen > 0 ? buflen - 1 : 0;
			}

			buf[pos++] = (char)ch;
		}

		count++;
	}

	if (mark_truncated && *str != '\0') {
		pos = logdict_append(buf, buflen, pos, "%%2E%%2E%%2E");
	}

	return pos;
}

static size_t logdict_string_limit(int precision)
{
	if (precision >= 0 &&
	    precision < CONFIG_LOG_DICTIONARY_MAX_STRLEN) {
		return (size_t)precision;
	}

	return CONFIG_LOG_DICTIONARY_MAX_STRLEN;
}

static int logdict_write_frame(const struct logdict_site_s *site,
			       const char *buf)
{
	if ((site->flags & LOGDICT_FLAG_PRINTF) != 0) {
		return printf("%s", buf);
	}

#ifdef CONFIG_LOGM
	if ((site->flags & LOGDICT_FLAG_LOWPUT) != 0) {
		return logm(LOGM_LOWPUT, LOGM_UNKNOWN, site->priority, "%s", buf);
	}

	return logm(LOGM_NORMAL, LOGM_UNKNOWN, site->priority, "%s", buf);
#else
	if ((site->flags & LOGDICT_FLAG_LOWPUT) != 0) {
#ifdef CONFIG_ARCH_LOWPUTC
		return lowsyslog(site->priority, "%s", buf);
#else
		return 0;
#endif
	}

	return syslog(site->priority, "%s", buf);
#endif
}

static bool logdict_should_emit(const struct logdict_site_s *site)
{
	if ((site->flags & LOGDICT_FLAG_PRINTF) != 0) {
		return true;
	}

	if ((site->flags & LOGDICT_FLAG_LOWPUT) != 0) {
#ifndef CONFIG_ARCH_LOWPUTC
		return false;
#endif
	}

#ifdef CONFIG_LOGM
	return true;
#else
	return (g_syslog_mask & LOG_MASK(site->priority)) != 0;
#endif
}

static unsigned int logdict_arg_type(uint64_t argdesc, unsigned int index)
{
	return (unsigned int)((argdesc >> (index * 4)) & 0x0f);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int logdict_text_emit(const struct logdict_site_s *site, va_list ap)
{
	char buf[CONFIG_LOG_DICTIONARY_TEXT_BUFSIZE];
	uint64_t argdesc;
	size_t pos;
	unsigned int index;
	int string_precision = -1;

	if (!site || site->magic != LOGDICT_SITE_MAGIC) {
		return 0;
	}

	if (!logdict_should_emit(site)) {
		return 0;
	}

	argdesc = ((uint64_t)site->argdesc_hi << 32) | site->argdesc_lo;

	pos = logdict_append(buf, sizeof(buf), 0,
			     "#TLOG|%d|%u|%08lx|%u|%u|%08lx%08lx",
			     LOGDICT_VERSION, (unsigned int)site->domain,
			     (unsigned long)site->id,
			     (unsigned int)site->priority,
			     (unsigned int)site->flags,
			     (unsigned long)site->argdesc_hi,
			     (unsigned long)site->argdesc_lo);

	for (index = 0; index < LOGDICT_MAX_ARGS; index++) {
		unsigned int type = logdict_arg_type(argdesc, index);

		if (type == LOGDICT_ARG_END) {
			break;
		}

		pos = logdict_append(buf, sizeof(buf), pos, "|");

		switch (type) {
		case LOGDICT_ARG_INT:
			pos = logdict_append(buf, sizeof(buf), pos, "%d",
					     va_arg(ap, int));
			break;
		case LOGDICT_ARG_UINT:
			pos = logdict_append(buf, sizeof(buf), pos, "%u",
					     va_arg(ap, unsigned int));
			break;
		case LOGDICT_ARG_LONG:
			pos = logdict_append(buf, sizeof(buf), pos, "%ld",
					     va_arg(ap, long));
			break;
		case LOGDICT_ARG_ULONG:
			pos = logdict_append(buf, sizeof(buf), pos, "%lu",
					     va_arg(ap, unsigned long));
			break;
		case LOGDICT_ARG_LONGLONG:
			pos = logdict_append(buf, sizeof(buf), pos, "%lld",
					     va_arg(ap, long long));
			break;
		case LOGDICT_ARG_ULONGLONG:
			pos = logdict_append(buf, sizeof(buf), pos, "%llu",
					     va_arg(ap, unsigned long long));
			break;
		case LOGDICT_ARG_PTR:
			pos = logdict_append(buf, sizeof(buf), pos, "%p",
					     va_arg(ap, void *));
			break;
		case LOGDICT_ARG_STRING:
			pos = logdict_append_escaped(buf, sizeof(buf), pos,
						     va_arg(ap, const char *),
						     logdict_string_limit(
							     string_precision),
						     string_precision < 0);
			string_precision = -1;
			break;
		case LOGDICT_ARG_CHAR:
			pos = logdict_append(buf, sizeof(buf), pos, "%d",
					     va_arg(ap, int));
			break;
		case LOGDICT_ARG_SIZE:
			pos = logdict_append(buf, sizeof(buf), pos, "%lu",
					     LOGDICT_VA_ARG_SIZE(ap));
			break;
		case LOGDICT_ARG_SSIZE:
			pos = logdict_append(buf, sizeof(buf), pos, "%ld",
					     LOGDICT_VA_ARG_SSIZE(ap));
			break;
		case LOGDICT_ARG_INTMAX:
			pos = logdict_append(buf, sizeof(buf), pos,
					     LOGDICT_INTMAX_FMT,
					     LOGDICT_VA_ARG_INTMAX(ap));
			break;
		case LOGDICT_ARG_UINTMAX:
			pos = logdict_append(buf, sizeof(buf), pos,
					     LOGDICT_UINTMAX_FMT,
					     LOGDICT_VA_ARG_UINTMAX(ap));
			break;
		case LOGDICT_ARG_STRPREC:
			string_precision = va_arg(ap, int);
			pos = logdict_append(buf, sizeof(buf), pos, "%d",
					     string_precision);
			break;
		default:
			pos = logdict_append(buf, sizeof(buf), pos, "?");
			break;
		}
	}

	if (pos + 1 < sizeof(buf)) {
		buf[pos++] = '\n';
		buf[pos] = '\0';
	} else if (sizeof(buf) > 1) {
		buf[sizeof(buf) - 2] = '\n';
		buf[sizeof(buf) - 1] = '\0';
	}

	return logdict_write_frame(site, buf);
}
