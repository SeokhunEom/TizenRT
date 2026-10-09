/****************************************************************************
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_ARMV8M_UP_FAULT_RECOVERY_H
#define __ARCH_ARM_SRC_ARMV8M_UP_FAULT_RECOVERY_H

#include <stdint.h>

/* A board must supply retained storage, readable RAM bounds and a reset path
 * before enabling this policy. Initially supported by QEMU MPS2-AN505 only.
 */
extern volatile uint32_t g_arm_fault_depth;
extern volatile uint32_t g_arm_fault_output_budget;
void arm_fault_boot_report(void);
void arm_fault_note_injection(uint32_t scenario);
void arm_fault_uart_timeout(void);

#endif
