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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#include <exc_return.h>

#if !defined(TEST_EXPECT_QEMU) || !defined(TEST_EXPECT_FPU) || \
	!defined(TEST_EXPECT_BASE) || !defined(TEST_EXPECT_HANDLER) || \
	!defined(TEST_EXPECT_PRIVTHR) || !defined(TEST_EXPECT_UNPRIVTHR)
#error "exception-return fixture is missing an expected value"
#elif defined(CONFIG_ARCH_FPU) != defined(CONFIG_ARM_CMNVECTOR)
#error "exception-return FPU fixture requires CONFIG_ARCH_FPU and CONFIG_ARM_CMNVECTOR together"
#else
#ifdef CONFIG_ARCH_CHIP_QEMU_ARMV8M
#define TEST_ACTUAL_QEMU 1
#else
#define TEST_ACTUAL_QEMU 0
#endif

#if defined(CONFIG_ARCH_FPU) && defined(CONFIG_ARM_CMNVECTOR)
#define TEST_ACTUAL_FPU 1
#else
#define TEST_ACTUAL_FPU 0
#endif

_Static_assert(TEST_ACTUAL_QEMU == TEST_EXPECT_QEMU,
	"exception-return fixture selected the wrong QEMU branch");
_Static_assert(TEST_ACTUAL_FPU == TEST_EXPECT_FPU,
	"exception-return fixture selected the wrong FPU branch");
_Static_assert(EXC_RETURN_BASE == TEST_EXPECT_BASE,
	"EXC_RETURN_BASE has the wrong value");
_Static_assert(EXC_RETURN_HANDLER == TEST_EXPECT_HANDLER,
	"EXC_RETURN_HANDLER has the wrong value");
_Static_assert(EXC_RETURN_PRIVTHR == TEST_EXPECT_PRIVTHR,
	"EXC_RETURN_PRIVTHR has the wrong value");
_Static_assert(EXC_RETURN_UNPRIVTHR == TEST_EXPECT_UNPRIVTHR,
	"EXC_RETURN_UNPRIVTHR has the wrong value");
#endif
