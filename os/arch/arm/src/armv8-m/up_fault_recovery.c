/****************************************************************************
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdint.h>
#include <stddef.h>
#include <debug.h>
#include <tinyara/security_level.h>
#include "nvic.h"
#include "up_arch.h"
#include "up_internal.h"
#include "up_fault_recovery.h"

#define FAULT_MAGIC 0x46524331u
#define FAULT_VERSION 2u
#define FAULT_OUTPUT_BUDGET 4096u
/* Stacking, unstacking, lazy preservation and stack-limit failures. */
#define FAULT_UNSAFE_FRAME 0x00103838u

struct arm_fault_snapshot_s {
	uint32_t vector;
	uint32_t exc_return;
	uint32_t msp;
	uint32_t psp;
	uint32_t masks[5]; /* PRIMASK, BASEPRI, CONTROL, MSPLIM, PSPLIM */
	uint32_t cfsr;
	uint32_t hfsr;
	uint32_t shcsr;
	uint32_t icsr;
	uint32_t bfar;
	uint32_t mmfar;
	uint32_t frame_valid;
	uint32_t frame[8]; /* R0-R3, R12, LR, PC, xPSR; valid only if frame_valid */
};

struct arm_fault_record_s {
	uint32_t magic; /* Written last, after payload and checksum. */
	uint32_t version;
	uint32_t sequence;
	uint32_t injection;
	uint32_t uart_timeout;
	struct arm_fault_snapshot_s snapshot;
	uint32_t checksum;
};

volatile uint32_t g_arm_fault_depth;
volatile uint32_t g_arm_fault_masks[5];
volatile uint32_t g_arm_fault_output_budget;
/* Independent commits: a partial secondary capture cannot invalidate first. */
static volatile struct arm_fault_record_s g_arm_fault_record[2]
	__attribute__((section(".fault_record"), aligned(8), used));

static uint32_t arm_fault_checksum(const volatile struct arm_fault_record_s *record)
{
	const volatile unsigned char *bytes = (const volatile unsigned char *)record;
	uint32_t sum = 2166136261u;
	unsigned int i;
	for (i = offsetof(struct arm_fault_record_s, version);
		 i < offsetof(struct arm_fault_record_s, checksum); i++) {
		sum = (sum ^ bytes[i]) * 16777619u;
	}
	return sum;
}

static int arm_fault_valid(const volatile struct arm_fault_record_s *record)
{
	return record->magic == FAULT_MAGIC && record->version == FAULT_VERSION &&
		record->checksum == arm_fault_checksum(record);
}

static void arm_fault_commit(volatile struct arm_fault_record_s *record)
{
	record->checksum = arm_fault_checksum(record);
	__asm__ __volatile__("dsb" : : : "memory");
	record->magic = FAULT_MAGIC;
	__asm__ __volatile__("dsb" : : : "memory");
}

/* This is a board capability, not a generic test of pointer validity. Only
 * QEMU's mapped RAM is accepted; MMIO, flash and overflow are rejected.
 * A hardware port needs its own RAM/MPU/security/retention contract.
 */
static int arm_fault_frame_readable(uint32_t sp)
{
	return (sp & 3) == 0 &&
		((sp >= 0x80000000u && sp <= 0x81000000u - 32) ||
		 (sp >= 0x10000000u && sp <= 0x10400000u - 32));
}

void arm_fault_note_injection(uint32_t scenario)
{
	g_arm_fault_record[0].magic = 0;
	g_arm_fault_record[0].injection = scenario;
	arm_fault_commit(&g_arm_fault_record[0]);
}

void arm_fault_uart_timeout(void)
{
	if (g_arm_fault_record[0].uart_timeout == 0) {
		g_arm_fault_record[0].magic = 0;
		g_arm_fault_record[0].uart_timeout = 1;
		arm_fault_commit(&g_arm_fault_record[0]);
	}
}

void __attribute__((noreturn)) arm_fault_recover(uint32_t vector,
		uint32_t exc_return, uint32_t msp, uint32_t psp)
{
	volatile struct arm_fault_record_s *record = &g_arm_fault_record[g_arm_fault_depth - 1];
	volatile struct arm_fault_snapshot_s *snapshot = &record->snapshot;
	volatile unsigned char *bytes = (volatile unsigned char *)record;
	uint32_t sp = (exc_return & 4) != 0 ? psp : msp;
	uint32_t sequence = g_arm_fault_record[0].sequence;
	unsigned int i;

	if (g_arm_fault_depth == 1) {
		sequence = arm_fault_valid(record) ? sequence + 1 : 1;
		g_arm_fault_record[1].magic = 0;
		g_arm_fault_output_budget = FAULT_OUTPUT_BUDGET;
	}
	for (i = 0; i < sizeof(*record); i++) {
		bytes[i] = 0;
	}
	record->version = FAULT_VERSION;
	record->sequence = sequence;
	snapshot->vector = vector;
	snapshot->exc_return = exc_return;
	snapshot->msp = msp;
	snapshot->psp = psp;
	for (i = 0; i < 5; i++) {
		snapshot->masks[i] = g_arm_fault_masks[i];
	}
	snapshot->cfsr = getreg32(NVIC_CFAULTS);
	snapshot->hfsr = getreg32(NVIC_HFAULTS);
	snapshot->shcsr = getreg32(NVIC_SYSHCON);
	snapshot->icsr = getreg32(NVIC_INTCTRL);
	snapshot->bfar = getreg32(NVIC_BFAULT_ADDR);
	snapshot->mmfar = getreg32(NVIC_MEMMANAGE_ADDR);
	if ((snapshot->cfsr & FAULT_UNSAFE_FRAME) == 0 &&
		(exc_return & 0xffffffe3u) == 0xffffffe1u &&
		(exc_return & 0x10) != 0 && arm_fault_frame_readable(sp)) {
		for (i = 0; i < 8; i++) {
			snapshot->frame[i] = ((volatile uint32_t *)sp)[i];
		}
		snapshot->frame_valid = 1;
	}
	arm_fault_commit(record);

#ifdef CONFIG_QEMU_FAULT_LAB
	if (vector == 6 && g_arm_fault_depth == 1) {
		extern void qemu_fault_lab_on_usagefault(void);
		/* Inject only AFTER the first committed diagnostic, before reset. */
		qemu_fault_lab_on_usagefault();
	}
#endif
	/* No heap, locks, file system, stack dump, UART flush or WDT disable.
	 * Once architectural lockup has occurred this code cannot run: a real
	 * board still needs an independent watchdog armed before any fault.
	 */
	up_systemreset();
	for (;;) {
	}
}

void arm_fault_boot_report(void)
{
	const volatile struct arm_fault_record_s *first = &g_arm_fault_record[0];
	const volatile struct arm_fault_record_s *second = &g_arm_fault_record[1];
	int secondary_valid = arm_fault_valid(second) && second->sequence == first->sequence;

	if (arm_fault_valid(first)) {
		/* Keep existing secure-diagnostic disclosure policy at normal boot. */
		if (IS_SECURE_STATE()) {
			lldbg("FAULTREC valid=1 diagnostic withheld by security policy\n");
			return;
		}
		lldbg("FAULTREC valid=1 seq=%u first=%u secondary=%u injection=%u uart_timeout=%u "
		      "frame_valid=%u pc=%08x cfsr=%08x secondary_frame_valid=%u "
		      "secondary_msp=%08x secondary_cfsr=%08x\n",
		      first->sequence, first->snapshot.vector,
		      secondary_valid ? second->snapshot.vector : 0, first->injection,
		      first->uart_timeout, first->snapshot.frame_valid,
		      first->snapshot.frame[6], first->snapshot.cfsr,
		      secondary_valid ? second->snapshot.frame_valid : 0,
		      secondary_valid ? second->snapshot.msp : 0,
		      secondary_valid ? second->snapshot.cfsr : 0);
	}
}
