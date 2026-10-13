/****************************************************************************
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <apps/shell/tash.h>
#include "up_xip_lab.h"

/* This is an opt-in destructive MPU experiment, NOT a SPIC emulator and NOT
 * a replacement production fault handler. Only the invoking core's memory
 * permissions change. No flash commands, erase or program are performed.
 */
#define RAMFUNC __attribute__((section(".image2.ram.text.xiplab"), noinline, noclone))
#define REG32(a) (*(volatile uint32_t *)(a))
#define MPU_TYPE  0xe000ed90u
#define MPU_CTRL  0xe000ed94u
#define MPU_RNR   0xe000ed98u
#define MPU_RBAR  0xe000ed9cu
#define MPU_RLAR  0xe000eda0u
#define MPU_MAIR0 0xe000edc0u
#define VTOR      0xe000ed08u
#define AIRCR     0xe000ed0cu
#define CFSR      0xe000ed28u
#define HFSR      0xe000ed2cu

#ifdef CONFIG_ARCH_CHIP_QEMU_ARMV8M
#define RAM_START 0x80000000u
#define RAM_END   0x80ffffffu
#define FLASH_START 0x10000000u
#define FLASH_END   0x1037ffffu
#else
#define RAM_START 0x10000000u
#define RAM_END   0x100fffffu
/* Include both the physical and remapped KM0/KM4 flash windows. */
#define FLASH_START 0x08000000u
#define FLASH_END   0x0fffffffu
extern void arm_xip_lab_cache_disable(void);
#endif

volatile struct xiplab_record_s g_xiplab_record
	__attribute__((section(".xiplab_record"), aligned(32)));
uint32_t g_xiplab_vectors[16] __attribute__((aligned(512)));
uint32_t g_xiplab_stack[XIPLAB_STACK_SIZE / 4] __attribute__((aligned(8)));
const uint32_t g_xiplab_flash_word = 0x46534c48u;

extern void xiplab_flash_entry(void);
extern void xiplab_ram_entry(void);
extern void xiplab_trigger(void) __attribute__((noreturn));
extern uint32_t xiplab_read_dependency(void);

static RAMFUNC uint32_t xiplab_checksum(void)
{
	volatile uint32_t *p = &g_xiplab_record.scenario;
	uint32_t value = XIPLAB_MAGIC;
	unsigned int i;
	for (i = 0; i < 9; i++) {
		value = (value << 5) | (value >> 27);
		value ^= p[i];
	}
	return value;
}

/* Everything reachable after enabling the MPU allowlist, including literal
 * pools and this function's callees, must reside in RAM. No UART/OS/heap use.
 */
RAMFUNC void xiplab_capture(void)
{
	uint32_t frame = g_xiplab_record.frame;
	g_xiplab_record.cfsr = REG32(CFSR);
	g_xiplab_record.hfsr = REG32(HFSR);
	g_xiplab_record.mpu_ctrl = REG32(MPU_CTRL);
	g_xiplab_record.vtor = REG32(VTOR);
	/* The lab injects a basic frame with FPU disabled. Never probe a frame
	 * if stacking failed or if it is outside the supported RAM windows.
	 */
	if (!(g_xiplab_record.cfsr & 0x00103838u) && !(frame & 3u) &&
		((frame >= RAM_START && frame <= RAM_END - 31u)
#ifdef CONFIG_ARCH_CHIP_RTL8721CSM
		 || (frame >= 0x02000000u && frame <= 0x023fffe0u)
#endif
		)) {
		g_xiplab_record.pc = REG32(frame + 24u);
	}
	if (g_xiplab_record.scenario == 2) {
		g_xiplab_record.stage = XIPLAB_DEPENDENCY;
		__asm__ volatile("dsb" ::: "memory");
		/* Expected DACCVIOL inside HardFault, then architectural lockup. */
		(void)xiplab_read_dependency();
	}
	g_xiplab_record.stage = XIPLAB_RESET;
	g_xiplab_record.checksum = xiplab_checksum();
	__asm__ volatile("dsb" ::: "memory");
	g_xiplab_record.magic = XIPLAB_MAGIC;
	__asm__ volatile("dsb" ::: "memory");
	REG32(AIRCR) = 0x05fa0004u;
	__asm__ volatile("dsb" ::: "memory");
	/* If reset is unavailable this is a reset-request wait, not evidence
	 * of flash bus stall. The host requires an actual guest reset event.
	 */
	for (;;) {
		__asm__ volatile("nop");
	}
}

static RAMFUNC void xiplab_region(uint32_t n, uint32_t start,
				 uint32_t end, uint32_t device)
{
	REG32(MPU_RNR) = n;
	/* Privileged RW; device regions are execute-never. */
	REG32(MPU_RBAR) = start | device;
	REG32(MPU_RLAR) = (end & ~31u) | (device << 1) | 1u;
}

static RAMFUNC __attribute__((noreturn)) void xiplab_arm(uint32_t scenario)
{
	unsigned int i;
	unsigned int regions = (REG32(MPU_TYPE) >> 8) & 255u;
	__asm__ volatile("cpsid i\n dsb\n isb" ::: "memory");
	REG32(VTOR) = (uint32_t)g_xiplab_vectors;
	REG32(MPU_CTRL) = 0;
	__asm__ volatile("dsb\n isb" ::: "memory");
	for (i = 0; i < regions; i++) {
		REG32(MPU_RNR) = i;
		REG32(MPU_RLAR) = 0;
	}
	/* Attribute 0: normal non-cacheable; attribute 1: Device-nGnRnE. */
	REG32(MPU_MAIR0) = 0x44u;
	xiplab_region(0, RAM_START, RAM_END, 0);
#ifdef CONFIG_ARCH_CHIP_RTL8721CSM
	xiplab_region(1, 0x02000000u, 0x023fffffu, 0);
#endif
	xiplab_region(2, 0x40000000u, 0x5fffffffu, 1);
	xiplab_region(3, 0xe0000000u, 0xe00fffffu, 1);
	if (scenario == 0) {
		xiplab_region(4, FLASH_START, FLASH_END, 0);
	}
	REG32(CFSR) = 0xffffffffu;
	REG32(HFSR) = 0xffffffffu;
	g_xiplab_record.stage = XIPLAB_ARMED;
	/* ENABLE | HFNMIENA; deliberately leave PRIVDEFENA clear. Without
	 * HFNMIENA, HardFault bypasses the MPU and produces a false pass.
	 */
	REG32(MPU_CTRL) = 3u;
	__asm__ volatile("dsb\n isb" ::: "memory");
	xiplab_trigger();
}

static int xiplab_in_ram(uint32_t address)
{
	return address >= RAM_START && address <= RAM_END;
}

static int xiplab_main(int argc, char **argv)
{
	uint32_t scenario;
	uint32_t control;
	uint32_t ipsr;
	unsigned int i;
	if (argc != 2 || strlen(argv[1]) != 1 || argv[1][0] < '0' || argv[1][0] > '3') {
		printf("usage: xiplab <0..3>\n"
		       "0: control, flash accessible -> record/reset\n"
		       "1: block flash, flash handler -> expected lockup\n"
		       "2: block flash, RAM handler reads flash -> expected lockup\n"
		       "3: block flash, complete RAM path -> record/reset\n"
		       "MPU permission fault model; NOT SPIC user mode or bus stall.\n"
		       "Cases 1/2 require board reset or power cycle.\n");
		return 0;
	}
	scenario = argv[1][0] - '0';
	__asm__ volatile("mrs %0, control\n mrs %1, ipsr" : "=r"(control), "=r"(ipsr));
	if ((control & 1u) || ipsr || ((REG32(MPU_TYPE) >> 8) & 255u) < 5 ||
	    !xiplab_in_ram((uint32_t)xiplab_arm) ||
	    !xiplab_in_ram((uint32_t)xiplab_ram_entry) ||
	    !xiplab_in_ram((uint32_t)xiplab_capture) ||
	    !xiplab_in_ram((uint32_t)xiplab_trigger) ||
	    !xiplab_in_ram((uint32_t)g_xiplab_vectors) ||
	    (uint32_t)xiplab_flash_entry < FLASH_START ||
	    (uint32_t)xiplab_flash_entry > FLASH_END ||
	    (uint32_t)&g_xiplab_flash_word < FLASH_START ||
	    (uint32_t)&g_xiplab_flash_word > FLASH_END) {
		printf("XIPLAB: rejected unsupported privilege/MPU/link layout\n");
		return -1;
	}
	printf("XIPLAB arm case=%u model=mpu flash=%08x..%08x handler=%08x dependency=%08x trigger=%08x record=%08x\n",
	       scenario, FLASH_START, FLASH_END,
	       (uint32_t)(scenario <= 1 ? xiplab_flash_entry : xiplab_ram_entry),
	       (uint32_t)&g_xiplab_flash_word, (uint32_t)xiplab_trigger,
	       (uint32_t)&g_xiplab_record);
	fflush(stdout);
#ifdef CONFIG_ARCH_CHIP_RTL8721CSM
	/* Flush/disable KM4 caches before changing normal memory attributes.
	 * Keep them off until reset, so the test cannot pass due to cache hits.
	 */
	arm_xip_lab_cache_disable();
#endif
	memset((void *)&g_xiplab_record, 0, sizeof(g_xiplab_record));
	g_xiplab_record.scenario = scenario;
	for (i = 0; i < 16; i++) {
		g_xiplab_vectors[i] = REG32(REG32(VTOR) + 4u * i);
	}
	g_xiplab_vectors[3] = (uint32_t)(scenario <= 1 ? xiplab_flash_entry : xiplab_ram_entry);
	xiplab_arm(scenario);
}

void arm_xip_lab_initialize(void)
{
	if (g_xiplab_record.magic == XIPLAB_MAGIC &&
	    g_xiplab_record.checksum == xiplab_checksum()) {
		printf("XIPLAB retained valid=1 case=%u stage=%u exception=%u pc=%08x cfsr=%08x hfsr=%08x mpu=%08x\n",
		       g_xiplab_record.scenario, g_xiplab_record.stage,
		       g_xiplab_record.exception, g_xiplab_record.pc,
		       g_xiplab_record.cfsr, g_xiplab_record.hfsr, g_xiplab_record.mpu_ctrl);
	}
	g_xiplab_record.magic = 0;
	tash_cmd_install("xiplab", xiplab_main, TASH_EXECMD_SYNC);
}
