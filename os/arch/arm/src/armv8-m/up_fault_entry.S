/****************************************************************************
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <tinyara/config.h>

	.syntax unified
	.thumb
	.text
	.global arm_fault_entry
	.type arm_fault_entry, %function
	.thumb_func
arm_fault_entry:
	/* Absolutely no push, BL or store through SP before switching stacks.
	 * Hardware stacking can already have failed: never dereference raw MSP.
	 */
	mrs r4, primask
	cpsid i
	mrs r0, ipsr
	mov r1, lr
	mrs r2, msp
	mrs r3, psp
	ldr r12, =g_arm_fault_masks
	str r4, [r12, #0]
	mrs r4, basepri
	str r4, [r12, #4]
	mrs r4, control
	str r4, [r12, #8]
	mrs r4, msplim
	str r4, [r12, #12]
	mrs r4, psplim
	str r4, [r12, #16]
	ldr r12, =g_arm_fault_depth
	ldr r4, [r12]
	cmp r4, #2
	bhs .Lreset
	adds r4, r4, #1
	str r4, [r12]
	/* A reentrant fault must not reuse the interrupted capture's C stack. */
	cmp r4, #1
	ite eq
	ldreq r4, =g_arm_fault_stack
	ldrne r4, =g_arm_fault_stack_secondary
	mov r5, #0
	msr msplim, r5
	add r5, r4, #1536
	msr msp, r5
	msr msplim, r4
	isb
	b arm_fault_recover
.Lreset:
	/* Last resort before any additional memory/console access. */
	ldr r0, =0xe000ed0c
	ldr r1, =0x05fa0004
	dsb
	str r1, [r0]
	dsb
1:
	b 1b
	.size arm_fault_entry, .-arm_fault_entry

	.bss
	.balign 8
g_arm_fault_stack:
	.skip 1536
g_arm_fault_stack_secondary:
	.skip 1536
