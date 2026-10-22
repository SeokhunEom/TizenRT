/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TRMM_TEST_DEBUG_H
#define TRMM_TEST_DEBUG_H
#include <stdio.h>
#define mvdbg(...) ((void)0)
#define mdbg(...) fprintf(stderr, __VA_ARGS__)
#endif
