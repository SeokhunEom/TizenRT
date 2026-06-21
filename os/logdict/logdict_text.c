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

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <syslog.h>

#include "logdict.h"

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
				     const char *str)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t count = 0;

	if (!str) {
		return logdict_append(buf, buflen, pos, "(null)");
	}

	while (*str != '\0' && count < CONFIG_LOG_DICTIONARY_MAX_STRLEN) {
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

	if (*str != '\0') {
		pos = logdict_append(buf, buflen, pos, "%%2E%%2E%%2E");
	}

	return pos;
}

static int logdict_write_frame(const struct logdict_site_s *site,
			       const char *buf)
{
	if ((site->flags & LOGDICT_FLAG_LOWPUT) != 0) {
#ifdef CONFIG_ARCH_LOWPUTC
		return lowsyslog(site->priority, "%s", buf);
#else
		return 0;
#endif
	}

	return syslog(site->priority, "%s", buf);
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

	if (!site || site->magic != LOGDICT_SITE_MAGIC) {
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
						     va_arg(ap, const char *));
			break;
		case LOGDICT_ARG_CHAR:
			pos = logdict_append(buf, sizeof(buf), pos, "%d",
					     va_arg(ap, int));
			break;
		case LOGDICT_ARG_SIZE:
			pos = logdict_append(buf, sizeof(buf), pos, "%lu",
					     (unsigned long)va_arg(ap, size_t));
			break;
		case LOGDICT_ARG_SSIZE:
			pos = logdict_append(buf, sizeof(buf), pos, "%ld",
					     (long)va_arg(ap, ssize_t));
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
