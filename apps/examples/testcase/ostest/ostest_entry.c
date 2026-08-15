/****************************************************************************
 * apps/examples/testcase/ostest/ostest_entry.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Keep the imported NuttX test driver unchanged while adapting its standard
 * main() symbol to the TizenRT built-in application registry.
 */

#define main ostest_main
#include "ostest_main.c"
