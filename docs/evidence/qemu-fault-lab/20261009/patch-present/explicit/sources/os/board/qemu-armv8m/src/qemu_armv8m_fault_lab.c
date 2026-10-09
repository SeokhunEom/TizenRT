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
#include "nvic.h"
#include "up_arch.h"

/* These symbols make the trigger and observations available to a debugger.
 * They are built only by the explicitly destructive QEMU fault_lab recipe.
 */
volatile uint32_t g_qemu_fault_lab_mode;
volatile uint32_t g_qemu_fault_lab_stage;
volatile uint32_t g_qemu_fault_lab_irq_pending;
volatile uint32_t g_qemu_fault_lab_nested_late;
volatile uint32_t g_qemu_fault_lab_outer_irq_count;
volatile uint32_t g_qemu_fault_lab_nested_irq_count;

#define FAULTLAB_IRQ_OUTER (MPS2_IRQ_FIRST + 70)
#define FAULTLAB_IRQ_NESTED (MPS2_IRQ_FIRST + 71)

/* A private, bounded process stack for the synthetic Thread/PSP context. */
static uint32_t g_qemu_fault_lab_psp_stack[512]
	__attribute__((aligned(8), used));

static int qemu_fault_lab_outer_irq(int irq, FAR void *context, FAR void *arg)
{
	(void)irq;
	(void)context;
	(void)arg;
	g_qemu_fault_lab_outer_irq_count++;
	if (g_qemu_fault_lab_nested_late != 0) {
		g_qemu_fault_lab_nested_late = 0;
		putreg32(1u << (71 & 31), NVIC_IRQ_PEND(71));
	}
	return OK;
}

static int qemu_fault_lab_nested_irq(int irq, FAR void *context, FAR void *arg)
{
	(void)irq;
	(void)context;
	(void)arg;
	g_qemu_fault_lab_nested_irq_count++;
	return OK;
}

/* Start one controlled nested-IRQ experiment from Thread mode on a private
 * bounded PSP stack, with MSP on the architecture's interrupt stack. The
 * QEMU-only assembly hook pends IRQ_NESTED at the first prologue window.
 */
__attribute__((naked, noreturn, noinline)) static void qemu_fault_lab_nested_launch(void)
{
	__asm__ volatile("ldr r0, =g_qemu_fault_lab_psp_stack\n"
			 "msr psplim, r0\n"
			 "ldr r1, =2048\n"
			 "add r0, r0, r1\n"
			 "msr psp, r0\n"
			 "ldr r0, =g_intstackalloc\n"
			 "msr msplim, r0\n"
			 "ldr r0, =g_intstackbase\n"
			 "msr msp, r0\n"
			 "movs r1, #2\n"
			 "msr control, r1\n"
			 "isb\n"
			 "ldr r0, =0xe000e208\n"
			 "movs r1, #0x40\n"
			 "str r1, [r0]\n"
			 ".global qemu_fault_lab_nested_spin\n"
			 "qemu_fault_lab_nested_spin:\n"
			 "b qemu_fault_lab_nested_spin\n");
}

static int qemu_fault_lab_nested_start(const char *scenario)
{
	int nested_priority;
	int ret;

	g_qemu_fault_lab_outer_irq_count = 0;
	g_qemu_fault_lab_nested_irq_count = 0;
	g_qemu_fault_lab_stage = 10;
	g_qemu_fault_lab_irq_pending = 0;
	g_qemu_fault_lab_nested_late = 0;
	if (strcmp(scenario, "early40") == 0) {
		nested_priority = NVIC_SYSH_PRIORITY_DEFAULT -
			NVIC_SYSH_PRIORITY_STEP * 2;
		g_qemu_fault_lab_irq_pending = 1;
	} else if (strcmp(scenario, "early00") == 0) {
		nested_priority = NVIC_SYSH_PRIORITY_MAX;
		g_qemu_fault_lab_irq_pending = 1;
	} else if (strcmp(scenario, "late40") == 0) {
		nested_priority = NVIC_SYSH_PRIORITY_DEFAULT -
			NVIC_SYSH_PRIORITY_STEP * 2;
		g_qemu_fault_lab_nested_late = 1;
	} else {
		printf("usage: faultlab nested early40|early00|late40\n");
		return -1;
	}
	ret = irq_attach(FAULTLAB_IRQ_OUTER, qemu_fault_lab_outer_irq, NULL);
	if (ret < 0) {
		return ret;
	}
	ret = irq_attach(FAULTLAB_IRQ_NESTED, qemu_fault_lab_nested_irq, NULL);
	if (ret < 0) {
		return ret;
	}
	up_enable_irq(MPS2_IRQ_USAGEFAULT);
	up_prioritize_irq(FAULTLAB_IRQ_OUTER, NVIC_SYSH_PRIORITY_DEFAULT);
	up_prioritize_irq(FAULTLAB_IRQ_NESTED, nested_priority);
	up_enable_irq(FAULTLAB_IRQ_OUTER);
	up_enable_irq(FAULTLAB_IRQ_NESTED);
	lldbg("FAULTLAB: nested scenario=%s outer=0x%x nested=0x%x priority=0x%x\n",
	      scenario, FAULTLAB_IRQ_OUTER, FAULTLAB_IRQ_NESTED, nested_priority);
	qemu_fault_lab_nested_launch();
	return OK;
}

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
	if (argc >= 2 && strcmp(argv[1], "nested") == 0) {
		if (argc != 3) {
			printf("usage: faultlab nested early40|early00|late40\n");
			return -1;
		}
		return qemu_fault_lab_nested_start(argv[2]);
	}
	if (argc != 2) {
		printf("usage: faultlab panic|lockup|uart|nested <scenario>\n");
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
