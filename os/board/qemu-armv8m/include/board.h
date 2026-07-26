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

#ifndef __BOARD_QEMU_ARMV8M_INCLUDE_BOARD_H
#define __BOARD_QEMU_ARMV8M_INCLUDE_BOARD_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BOARD_SYSCLK_FREQUENCY          20000000
#define BOARD_UART0_BAUD                115200

#define LED_STARTED                     0
#define LED_HEAPALLOCATE                0
#define LED_IRQSENABLED                 0
#define LED_STACKCREATED                0
#define LED_INIRQ                       0
#define LED_SIGNAL                      0
#define LED_ASSERTION                   0
#define LED_PANIC                       0

#endif /* __BOARD_QEMU_ARMV8M_INCLUDE_BOARD_H */
