/****************************************************************************
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_ARMV8M_UP_XIP_LAB_H
#define __ARCH_ARM_SRC_ARMV8M_UP_XIP_LAB_H

/* Shared with the stackless entry and the host evidence reader. */
#define XIPLAB_MAGIC         0x58495031
#define XIPLAB_ARMED         10
#define XIPLAB_ENTERED       20
#define XIPLAB_DEPENDENCY     22
#define XIPLAB_RESET         30
#define XIPLAB_STACK_SIZE    1024

#define XIPLAB_REC_MAGIC     0
#define XIPLAB_REC_CASE      4
#define XIPLAB_REC_STAGE     8
#define XIPLAB_REC_EXCEPTION 12
#define XIPLAB_REC_FRAME     16

#ifndef __ASSEMBLY__
#include <stdint.h>

struct xiplab_record_s {
	uint32_t magic;
	uint32_t scenario;
	uint32_t stage;
	uint32_t exception;
	uint32_t frame;
	uint32_t pc;
	uint32_t cfsr;
	uint32_t hfsr;
	uint32_t mpu_ctrl;
	uint32_t vtor;
	uint32_t checksum;
};

void arm_xip_lab_initialize(void);
#endif
#endif
