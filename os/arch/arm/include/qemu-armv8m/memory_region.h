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

#ifndef __ARCH_ARM_INCLUDE_QEMU_ARMV8M_MEMORY_REGION_H
#define __ARCH_ARM_INCLUDE_QEMU_ARMV8M_MEMORY_REGION_H

#include <stdint.h>

/* mps2-an505.ld exports the kernel's writable static roots. Heap payloads
 * are scanned separately by the checker; .bss stops before the idle stack.
 */

#define MEM_VAR_REGION_COUNT 2

extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _sdata;
extern uint32_t _edata;

static void *const variable_region_start_addr[MEM_VAR_REGION_COUNT] = {&_sbss, &_sdata};
static void *const variable_region_end_addr[MEM_VAR_REGION_COUNT] = {&_ebss, &_edata};

#endif /* __ARCH_ARM_INCLUDE_QEMU_ARMV8M_MEMORY_REGION_H */
