/****************************************************************************
 * apps/examples/ltp/config.h
 *
 * Feature detection macros for LTP on TizenRT.
 *
 * Adapted from NuttX apps/testing/ltp/config.h for TizenRT.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language
 * governing permissions and limitations under the License.
 *
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_LTP_CONFIG_H
#define __APPS_EXAMPLES_LTP_CONFIG_H

/* TizenRT does not define SIGTERM in its signal.h. It uses non-standard
 * signal numbers (SIGUSR1=1, SIGUSR2=2, SIGALRM=3, SIGKILL=9, etc.).
 * Define SIGTERM as SIGKILL so that LTP tests using it can compile.
 */
#ifndef SIGTERM
#define SIGTERM SIGKILL
#endif

/* Define to 1 if you have __atomic_* compiler builtins */
#define HAVE_ATOMIC_MEMORY_MODEL 1


/* Define to 1 if you have __builtin___clear_cache */
#define HAVE_BUILTIN_CLEAR_CACHE 1

/* Define to 1 if you have the `daemon' function. */
/* #undef HAVE_DAEMON */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <pthread.h> header file. */
#define HAVE_PTHREAD_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if `sa_sigaction' is a member of `struct sigaction'. */
#define HAVE_STRUCT_SIGACTION_SA_SIGACTION 1

/* Define to 1 if the system has the type `struct iovec'. */
#define HAVE_STRUCT_IOVEC 1

/* Define to 1 if you have __sync_add_and_fetch */
#define HAVE_SYNC_ADD_AND_FETCH 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `vfork' function. */
/* #undef HAVE_VFORK */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Target is running without an MMU */
#define UCLINUX 1

/* Name of package */
#define PACKAGE "ltp"

/* Define to the full name of this package. */
#define PACKAGE_NAME "ltp"

/* Define to the version of this package. */
#define PACKAGE_VERSION "20230516"

/* Version number of package */
#define VERSION "20230516"

#endif /* __APPS_EXAMPLES_LTP_CONFIG_H */
