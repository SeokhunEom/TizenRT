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

#ifndef __ARCH_ARM_INCLUDE_QEMU_ARMV8M_CHIP_H
#define __ARCH_ARM_INCLUDE_QEMU_ARMV8M_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MPS2_AN505_SYSCLK_FREQUENCY     20000000

#define MPS2_AN505_SSRAM_BASE           0x10000000
#define MPS2_AN505_SSRAM_SIZE           0x00400000
#define MPS2_AN505_RAM_BASE             0x80000000
#define MPS2_AN505_RAM_SIZE             0x01000000

#define MPS2_UART0_BASE                 0x40200000
#define MPS2_TIMER0_BASE                0x40000000

#define MPS2_TIMER_CTRL_OFFSET          0x0000
#define MPS2_TIMER_VALUE_OFFSET         0x0004
#define MPS2_TIMER_RELOAD_OFFSET        0x0008
#define MPS2_TIMER_INTSTATUS_OFFSET     0x000c

#define MPS2_TIMER0_CTRL                (MPS2_TIMER0_BASE + MPS2_TIMER_CTRL_OFFSET)
#define MPS2_TIMER0_VALUE               (MPS2_TIMER0_BASE + MPS2_TIMER_VALUE_OFFSET)
#define MPS2_TIMER0_RELOAD              (MPS2_TIMER0_BASE + MPS2_TIMER_RELOAD_OFFSET)
#define MPS2_TIMER0_INTSTATUS           (MPS2_TIMER0_BASE + MPS2_TIMER_INTSTATUS_OFFSET)

#define MPS2_TIMER_CTRL_EN              (1 << 0)
#define MPS2_TIMER_CTRL_IRQEN           (1 << 3)
#define MPS2_TIMER_INTSTATUS_IRQ        (1 << 0)

#define MPS2_UART_DATA_OFFSET           0x0000
#define MPS2_UART_STATE_OFFSET          0x0004
#define MPS2_UART_CTRL_OFFSET           0x0008
#define MPS2_UART_INTSTATUS_OFFSET      0x000c
#define MPS2_UART_BAUDDIV_OFFSET        0x0010

#define MPS2_UART0_DATA                 (MPS2_UART0_BASE + MPS2_UART_DATA_OFFSET)
#define MPS2_UART0_STATE                (MPS2_UART0_BASE + MPS2_UART_STATE_OFFSET)
#define MPS2_UART0_CTRL                 (MPS2_UART0_BASE + MPS2_UART_CTRL_OFFSET)
#define MPS2_UART0_INTSTATUS            (MPS2_UART0_BASE + MPS2_UART_INTSTATUS_OFFSET)
#define MPS2_UART0_BAUDDIV              (MPS2_UART0_BASE + MPS2_UART_BAUDDIV_OFFSET)

#define MPS2_UART_STATE_TXFULL          (1 << 0)
#define MPS2_UART_STATE_RXFULL          (1 << 1)
#define MPS2_UART_STATE_TXOVERRUN       (1 << 2)
#define MPS2_UART_STATE_RXOVERRUN       (1 << 3)

#define MPS2_UART_CTRL_TXEN             (1 << 0)
#define MPS2_UART_CTRL_RXEN             (1 << 1)
#define MPS2_UART_CTRL_TXINTEN          (1 << 2)
#define MPS2_UART_CTRL_RXINTEN          (1 << 3)
#define MPS2_UART_CTRL_TXOVRINTEN       (1 << 4)
#define MPS2_UART_CTRL_RXOVRINTEN       (1 << 5)

#define MPS2_UART_INT_TX                (1 << 0)
#define MPS2_UART_INT_RX                (1 << 1)
#define MPS2_UART_INT_TXOVR             (1 << 2)
#define MPS2_UART_INT_RXOVR             (1 << 3)
#define MPS2_UART_INT_ALL               (MPS2_UART_INT_TX | MPS2_UART_INT_RX | \
					 MPS2_UART_INT_TXOVR | MPS2_UART_INT_RXOVR)

#define NVIC_SYSH_PRIORITY_MIN          0xe0
#define NVIC_SYSH_PRIORITY_DEFAULT      0x80
#define NVIC_SYSH_PRIORITY_MAX          0x00
#define NVIC_SYSH_PRIORITY_STEP         0x20

#if defined(CONFIG_ARCH_HIPRI_INTERRUPT) && defined(CONFIG_ARCH_INT_DISABLEALL)
#define NVIC_SYSH_MAXNORMAL_PRIORITY    (NVIC_SYSH_PRIORITY_MAX + \
					 2 * NVIC_SYSH_PRIORITY_STEP)
#define NVIC_SYSH_HIGH_PRIORITY         (NVIC_SYSH_PRIORITY_MAX + \
					 NVIC_SYSH_PRIORITY_STEP)
#define NVIC_SYSH_DISABLE_PRIORITY      NVIC_SYSH_HIGH_PRIORITY
#define NVIC_SYSH_SVCALL_PRIORITY       NVIC_SYSH_PRIORITY_MAX
#else
#define NVIC_SYSH_MAXNORMAL_PRIORITY    (NVIC_SYSH_PRIORITY_MAX + \
					 NVIC_SYSH_PRIORITY_STEP)
#define NVIC_SYSH_HIGH_PRIORITY         NVIC_SYSH_PRIORITY_MAX
#define NVIC_SYSH_DISABLE_PRIORITY      NVIC_SYSH_MAXNORMAL_PRIORITY
#define NVIC_SYSH_SVCALL_PRIORITY       NVIC_SYSH_PRIORITY_MAX
#endif

#define ARMV8M_PERIPHERAL_INTERRUPTS    92

#endif /* __ARCH_ARM_INCLUDE_QEMU_ARMV8M_CHIP_H */
