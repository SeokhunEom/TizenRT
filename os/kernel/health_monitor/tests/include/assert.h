/* SPDX-License-Identifier: Apache-2.0 */
/* Keep host assertions; provide the target-only fatal macro for host builds. */
#include_next <assert.h>
#include <stdlib.h>
#ifndef PANIC
#define PANIC() abort()
#endif
