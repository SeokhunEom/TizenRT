/****************************************************************************
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <debug.h>
#include <tinyara/irq.h>
#include <tinyara/arch.h>
#include <arch/irq.h>
#include <apps/shell/tash.h>
#include <arch/chip/chip.h>
#include "up_arch.h"

/* These symbols make the trigger and observations available to a debugger.
 * They are built only by the explicitly destructive QEMU fault_lab recipe.
 */
volatile uint32_t g_qemu_fault_lab_mode;
volatile uint32_t g_qemu_fault_lab_stage;

__attribute__((noinline)) void qemu_fault_lab_checkpoint(void)
{
	__asm__ volatile("nop" ::: "memory");
}

__attribute__((naked, noinline)) void qemu_fault_lab_trigger(void)
{
	__asm__ volatile("udf #0\n\tbx lr");
}

__attribute__((naked, noinline, noreturn)) void qemu_fault_lab_bad_stack(void)
{
	/* Explicit fault injection, not a claim that the historical race always
	 * produces this address. A second UDF escalates while UsageFault is active;
	 * HardFault then cannot execute its software save on this invalid MSP.
	 */
	__asm__ volatile("ldr r0, =0x60000000\n\tmsr msp, r0\n\tudf #1\n\tb .");
}

void qemu_fault_lab_on_usagefault(void)
{
	g_qemu_fault_lab_stage = 2;
	qemu_fault_lab_checkpoint();
	if (g_qemu_fault_lab_mode == 2) {
		lldbg("FAULTLAB: injecting invalid MSP and secondary fault\n");
		g_qemu_fault_lab_stage = 3;
		qemu_fault_lab_checkpoint();
		qemu_fault_lab_bad_stack();
	} else if (g_qemu_fault_lab_mode == 3) {
		lldbg("FAULTLAB: disabling UART TX and filling its buffer\n");
		putreg32(getreg32(MPS2_UART0_CTRL) & ~MPS2_UART_CTRL_TXEN,
			 MPS2_UART0_CTRL);
		putreg32('!', MPS2_UART0_DATA);
		g_qemu_fault_lab_stage = 3;
		qemu_fault_lab_checkpoint();
		/* Exercise the real polling driver, without replacing its loop. */
		lldbg("FAULTLAB: this output cannot finish\n");
	}
}

static int qemu_fault_lab_command(int argc, char **argv)
{
	uint32_t mode;
	if (argc != 2) {
		printf("usage: faultlab panic|lockup|uart\n");
		return -1;
	}
	if (strcmp(argv[1], "panic") == 0) {
		mode = 1;
	} else if (strcmp(argv[1], "lockup") == 0) {
		mode = 2;
	} else if (strcmp(argv[1], "uart") == 0) {
		mode = 3;
	} else {
		return -1;
	}
	lldbg("FAULTLAB: begin %s\n", argv[1]);
	g_qemu_fault_lab_mode = mode;
	g_qemu_fault_lab_stage = 1;
	up_enable_irq(MPS2_IRQ_USAGEFAULT);
	qemu_fault_lab_checkpoint();
	qemu_fault_lab_trigger();
	return -1;
}

void qemu_fault_lab_initialize(void)
{
	tash_cmd_install("faultlab", qemu_fault_lab_command, TASH_EXECMD_SYNC);
}
