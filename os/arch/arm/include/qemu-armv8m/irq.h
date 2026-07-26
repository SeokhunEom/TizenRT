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

#ifndef __ARCH_ARM_INCLUDE_QEMU_ARMV8M_IRQ_H
#define __ARCH_ARM_INCLUDE_QEMU_ARMV8M_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <arch/qemu-armv8m/chip.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MPS2_IRQ_RESERVED               0
#define MPS2_IRQ_NMI                    2
#define MPS2_IRQ_HARDFAULT              3
#define MPS2_IRQ_MEMFAULT               4
#define MPS2_IRQ_BUSFAULT               5
#define MPS2_IRQ_USAGEFAULT             6
#define MPS2_IRQ_SVCALL                 11
#define MPS2_IRQ_DBGMONITOR             12
#define MPS2_IRQ_PENDSV                 14
#define MPS2_IRQ_SYSTICK                15

#define MPS2_IRQ_FIRST                  16
#define MPS2_IRQ_TIMER0                 (MPS2_IRQ_FIRST + 3)
#define MPS2_IRQ_UART0_RX               (MPS2_IRQ_FIRST + 32)
#define MPS2_IRQ_UART0_TX               (MPS2_IRQ_FIRST + 33)
#define MPS2_IRQ_UART0_COMBINED         (MPS2_IRQ_FIRST + 42)

#define NR_IRQS                         (MPS2_IRQ_FIRST + ARMV8M_PERIPHERAL_INTERRUPTS)
#define NR_VECTORS                      (NR_IRQS - MPS2_IRQ_FIRST)

#endif /* __ARCH_ARM_INCLUDE_QEMU_ARMV8M_IRQ_H */
