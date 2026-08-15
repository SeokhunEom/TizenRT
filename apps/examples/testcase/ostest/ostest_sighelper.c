/****************************************************************************
 * apps/examples/testcase/ostest/ostest_sighelper.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <tinyara/config.h>

#include <stdbool.h>
#include <signal.h>

#include "ostest.h"

/* Apache NuttX represents sigset_t as an element array.  TizenRT represents
 * the same public signal set as a single 32-bit value.
 */

bool sigset_isequal(FAR const sigset_t *left, FAR const sigset_t *right)
{
  return *left == *right;
}
