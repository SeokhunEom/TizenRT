/****************************************************************************
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <tinyara/config.h>
#include "up_xip_lab.h"

	.syntax unified
	.thumb

	/* Case 1 fails before the first instruction here can execute. Case 0
	 * uses this exact entry with flash accessible as a positive control.
	 */
	.section .text.xiplab_flash_entry,"ax",%progbits
	.global xiplab_flash_entry
	.type xiplab_flash_entry, %function
	.thumb_func
xiplab_flash_entry:
	b xiplab_ram_entry
	.size xiplab_flash_entry, .-xiplab_flash_entry

	.section .image2.ram.text.xiplab,"ax",%progbits
	.global xiplab_ram_entry
	.type xiplab_ram_entry, %function
	.thumb_func
xiplab_ram_entry:
	cpsid i
	ldr r0, =g_xiplab_record
	movs r1, #XIPLAB_ENTERED
	str r1, [r0, #XIPLAB_REC_STAGE]
	mrs r1, ipsr
	str r1, [r0, #XIPLAB_REC_EXCEPTION]
	tst lr, #4
	ite eq
	mrseq r1, msp
	mrsne r1, psp
	str r1, [r0, #XIPLAB_REC_FRAME]
	/* Do not depend on the interrupted task's stack for capture/reset. */
	movs r1, #0
	msr msplim, r1
	ldr r1, =g_xiplab_stack
	add r2, r1, #XIPLAB_STACK_SIZE
	msr msp, r2
	msr msplim, r1
	isb
	b xiplab_capture
	.ltorg
	.size xiplab_ram_entry, .-xiplab_ram_entry

	.global xiplab_trigger
	.type xiplab_trigger, %function
	.thumb_func
xiplab_trigger:
	udf #0
	b .
	.size xiplab_trigger, .-xiplab_trigger

	/* A real load, not a compiler-folded constant or a software spin. */
	.global xiplab_read_dependency
	.type xiplab_read_dependency, %function
	.thumb_func
xiplab_read_dependency:
	ldr r0, =g_xiplab_flash_word
	.global xiplab_dependency_load
xiplab_dependency_load:
	ldr r0, [r0]
	bx lr
	.ltorg
	.size xiplab_read_dependency, .-xiplab_read_dependency
