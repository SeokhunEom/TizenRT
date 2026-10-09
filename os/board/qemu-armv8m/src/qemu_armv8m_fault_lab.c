/****************************************************************************
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>
#ifdef CONFIG_ARMV8M_FAULT_RECOVERY
#include "up_fault_recovery.h"
#endif
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

#ifdef CONFIG_ARCH_NESTED_INTERRUPT
static uint32_t g_qemu_fault_lab_saved[6] __attribute__((used));
static volatile uint32_t g_qemu_fault_lab_progress;

static int qemu_fault_lab_progress_task(int argc, char **argv)
{
	g_qemu_fault_lab_progress++;
	return OK;
}

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
__attribute__((naked, noinline)) static void qemu_fault_lab_nested_launch(void)
{
	__asm__ volatile("mrs r3, primask\n"
			 "cpsid i\n"
			 "push {r4-r11, lr}\n"
			 "ldr r0, =g_qemu_fault_lab_saved\n"
			 "mrs r1, msp\n"
			 "str r1, [r0, #0]\n"
			 "mrs r1, psp\n"
			 "str r1, [r0, #4]\n"
			 "mrs r1, control\n"
			 "str r1, [r0, #8]\n"
			 "mrs r1, msplim\n"
			 "str r1, [r0, #12]\n"
			 "mrs r1, psplim\n"
			 "str r1, [r0, #16]\n"
			 "str r3, [r0, #20]\n"
			 "ldr r0, =g_qemu_fault_lab_psp_stack\n"
			 "msr psplim, r0\n"
			 "add r0, r0, #2048\n"
			 "msr psp, r0\n"
			 "movs r0, #0\n"
			 "msr msplim, r0\n"
			 "ldr r0, =g_intstackbase\n"
			 "msr msp, r0\n"
			 "ldr r0, =g_intstackalloc\n"
			 "msr msplim, r0\n"
			 "movs r1, #2\n"
			 "msr control, r1\n"
			 "isb\n"
			 "ldr r0, =0xe000e208\n"
			 "movs r1, #0x40\n"
			 "str r1, [r0]\n"
			 "dsb\n"
			 "msr primask, r3\n"
			 "isb\n"
			 ".global qemu_fault_lab_nested_spin\n"
			 "qemu_fault_lab_nested_spin:\n"
			 "ldr r0, =g_qemu_fault_lab_nested_irq_count\n"
			 "ldr r0, [r0]\n"
			 "cmp r0, #1\n"
			 "bne qemu_fault_lab_nested_spin\n"
			 "cpsid i\n"
			 "ldr r0, =g_qemu_fault_lab_saved\n"
			 "movs r1, #0\n"
			 "msr msplim, r1\n"
			 "msr psplim, r1\n"
			 "ldr r1, [r0, #0]\n"
			 "msr msp, r1\n"
			 "ldr r1, [r0, #4]\n"
			 "msr psp, r1\n"
			 "ldr r1, [r0, #8]\n"
			 "msr control, r1\n"
			 "isb\n"
			 "ldr r1, [r0, #12]\n"
			 "msr msplim, r1\n"
			 "ldr r1, [r0, #16]\n"
			 "msr psplim, r1\n"
			 "ldr r1, [r0, #20]\n"
			 "pop {r4-r11, lr}\n"
			 "msr primask, r1\n"
			 "bx lr\n");
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
	/* Do not let a tick switch the TCB to our short-lived synthetic stack. */
	sched_lock();
	qemu_fault_lab_nested_launch();
	sched_unlock();
	up_disable_irq(FAULTLAB_IRQ_OUTER);
	up_disable_irq(FAULTLAB_IRQ_NESTED);
	printf("FAULTLAB: returned outer=%u nested=%u\n",
	       g_qemu_fault_lab_outer_irq_count, g_qemu_fault_lab_nested_irq_count);
	g_qemu_fault_lab_progress = 0;
	ret = task_create("fault-progress", 100, 2048,
	                  qemu_fault_lab_progress_task, NULL);
	if (ret < 0) {
		return ret;
	}
	for (int i = 0; i < 100 && g_qemu_fault_lab_progress == 0; i++) {
		usleep(1000);
	}
	if (g_qemu_fault_lab_progress == 1) {
		printf("FAULTLAB: scheduler progressed\n");
		return OK;
	}
	return -1;
}

#else
static int qemu_fault_lab_nested_start(const char *scenario)
{
	printf("faultlab 1-3 require the fault_lab_nested configuration\n");
	return -1;
}
#endif

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
	/* Clear the emergency stack's limit so this still exercises unmapped
	 * MSP, rather than being intercepted earlier by MSPLIM's STKOF check.
	 */
	__asm__ volatile("movs r1, #0\n\tmsr msplim, r1\n\t"
			 "ldr r0, =0x60000000\n\tmsr msp, r0\n\tudf #1\n\tb .");
}

void qemu_fault_lab_on_usagefault(void)
{
	g_qemu_fault_lab_stage = 2;
	qemu_fault_lab_checkpoint();
	if (g_qemu_fault_lab_mode == 2) {
		lldbg("FAULTLAB: injecting invalid MSP and secondary fault\n");
		g_qemu_fault_lab_stage = 3;
		qemu_fault_lab_checkpoint();
#ifdef CONFIG_ARMV8M_FAULT_RECOVERY
		arm_fault_note_injection(5);
#endif
		qemu_fault_lab_bad_stack();
	} else if (g_qemu_fault_lab_mode == 3) {
		lldbg("FAULTLAB: disabling UART TX and filling its buffer\n");
		putreg32(getreg32(MPS2_UART0_CTRL) & ~MPS2_UART_CTRL_TXEN,
			 MPS2_UART0_CTRL);
		putreg32('!', MPS2_UART0_DATA);
		g_qemu_fault_lab_stage = 3;
		qemu_fault_lab_checkpoint();
#ifdef CONFIG_ARMV8M_FAULT_RECOVERY
		arm_fault_note_injection(6);
#endif
		/* Exercise the real polling driver, without replacing its loop. */
		lldbg("FAULTLAB: this output cannot finish\n");
	}
}

static int qemu_fault_lab_command(int argc, char **argv)
{
	uint32_t mode;
	const char *scenario;
	if (argc == 1) {
		printf("usage: faultlab <1..6>\n");
		printf("  1 early40 nested IRQ during exception entry (priority 0x40)\n");
		printf("  2 early00 nested IRQ during exception entry (priority 0x00)\n");
		printf("  3 late40 nested IRQ after interrupt-stack switch\n");
		printf("  4 UsageFault panic halt injection\n");
		printf("  5 invalid-MSP architectural lockup injection\n");
		printf("  6 UART polling wait injection\n");
		printf("Cases 1-3 return; 4-6 inject fatal faults (reboot with recovery enabled).\n");
		return OK;
	}
	if (argc == 2) {
		if (strcmp(argv[1], "1") == 0) {
			scenario = "early40";
			return qemu_fault_lab_nested_start(scenario);
		} else if (strcmp(argv[1], "2") == 0) {
			scenario = "early00";
			return qemu_fault_lab_nested_start(scenario);
		} else if (strcmp(argv[1], "3") == 0) {
			scenario = "late40";
			return qemu_fault_lab_nested_start(scenario);
		}
	}
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
	} else if (strcmp(argv[1], "4") == 0) {
		mode = 1;
	} else if (strcmp(argv[1], "5") == 0) {
		mode = 2;
	} else if (strcmp(argv[1], "6") == 0) {
		mode = 3;
	} else {
		printf("unknown scenario; run 'faultlab' to list cases\n");
		return -1;
	}
	lldbg("FAULTLAB: begin scenario %s (%s)\n", argv[1],
	      mode == 1 ? "panic" : (mode == 2 ? "lockup" : "uart"));
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
