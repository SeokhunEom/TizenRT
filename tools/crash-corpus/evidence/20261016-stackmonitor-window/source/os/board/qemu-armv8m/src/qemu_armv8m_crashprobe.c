/****************************************************************************
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <tinyara/config.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <tinyara/arch.h>
#include <tinyara/mm/mm.h>
#include <apps/shell/tash.h>
#include <arch/irq.h>
#include <arch/chip/chip.h>
#include "up_arch.h"
#include "nvic.h"
#include "mpu.h"

/* These probes are deliberately destructive and exist only in log_analyzer.
 * Inline assembly prevents the compiler from replacing undefined C behavior
 * with unrelated instructions. Each crash must run in a fresh QEMU process.
 */
static volatile uint32_t g_crashprobe_sink;
static uint32_t g_crashprobe_alignment[2];
static uint32_t g_crashprobe_mpu[8] __attribute__((aligned(32)));

__attribute__((noinline)) static void crashprobe_stack(unsigned int depth)
{
	volatile uint8_t frame[256];
	frame[0] = (uint8_t)depth;
	frame[255] = (uint8_t)(depth + 1);
	/* Volatile read preserves a recursive call while suppressing tail calls. */
	if (depth != g_crashprobe_sink) {
		crashprobe_stack(depth + 1);
	}
	g_crashprobe_sink += frame[0] + frame[255];
}

static int crashprobe_main(int argc, char **argv)
{
	if (argc != 2) {
		printf("usage: crashprobe assert|udf|divzero|unaligned|busfault|hardfault|invstate|memfault|execfault|stack|oom|doublefree|heapcorrupt|histories\n");
		return -1;
	}
	printf("CRASHPROBE BEGIN %s (QEMU-only intentional fault)\n", argv[1]);
	fflush(stdout);
	up_enable_irq(MPS2_IRQ_USAGEFAULT);
	up_enable_irq(MPS2_IRQ_BUSFAULT);
	if (strcmp(argv[1], "assert") == 0) {
		ASSERT(0);
	} else if (strcmp(argv[1], "udf") == 0) {
		__asm__ volatile("udf #0");
	} else if (strcmp(argv[1], "hardfault") == 0) {
		up_disable_irq(MPS2_IRQ_USAGEFAULT);
		__asm__ volatile("udf #0");
	} else if (strcmp(argv[1], "divzero") == 0) {
		modifyreg32(NVIC_CFGCON, 0, NVIC_CFGCON_DIV0TRP);
		__asm__ volatile("movs r0, #1\n\tmovs r1, #0\n\tudiv r2, r0, r1" ::: "r0", "r1", "r2");
	} else if (strcmp(argv[1], "unaligned") == 0) {
		uintptr_t address = (uintptr_t)g_crashprobe_alignment + 1;
		modifyreg32(NVIC_CFGCON, 0, NVIC_CFGCON_UNALIGNTRP);
		__asm__ volatile("ldr r0, [%0]" :: "r"(address) : "r0", "memory");
	} else if (strcmp(argv[1], "busfault") == 0) {
		uintptr_t address = 0x60000000;
		__asm__ volatile("ldr r0, [%0]" :: "r"(address) : "r0", "memory");
	} else if (strcmp(argv[1], "invstate") == 0) {
		uintptr_t address = (uintptr_t)crashprobe_main & ~1u;
		__asm__ volatile("blx %0" :: "r"(address) : "lr", "memory");
	} else if (strcmp(argv[1], "memfault") == 0 || strcmp(argv[1], "execfault") == 0) {
		uintptr_t address = (uintptr_t)g_crashprobe_mpu;
		bool execute = strcmp(argv[1], "execfault") == 0;
		/* Valid Thumb return bytes for the execute-never probe. */
		g_crashprobe_mpu[0] = 0x47704770;
		putreg32(7, MPU_RNR);
		putreg32(address | MPU_RBAR_XN | (execute ? MPU_RBAR_AP_RWNO : MPU_RBAR_AP_RONO), MPU_RBAR);
		putreg32(address | MPU_RLAR_ENABLE, MPU_RLAR);
		mpu_control(true, false, true);
		__asm__ volatile("dsb\n\tisb" ::: "memory");
		if (execute) {
			address |= 1;
			__asm__ volatile("blx %0" :: "r"(address) : "lr", "memory");
		} else {
			__asm__ volatile("str %0, [%0]" :: "r"(address) : "memory");
		}
	} else if (strcmp(argv[1], "stack") == 0) {
		g_crashprobe_sink = 0;
		crashprobe_stack(1);
	} else if (strcmp(argv[1], "oom") == 0) {
		void *memory = malloc(32 * 1024 * 1024);
		printf("CRASHPROBE allocation returned %p\n", memory);
		free(memory);
	} else if (strcmp(argv[1], "doublefree") == 0) {
		void *before = malloc(128);
		void *memory = malloc(128);
		void *after = malloc(128);
		ASSERT(before && memory && after);
		free(memory);
		free(memory);
		free(before);
		free(after);
	} else if (strcmp(argv[1], "heapcorrupt") == 0) {
		void *memory = malloc(128);
		struct mm_allocnode_s *node;
		ASSERT(memory != NULL);
		node = (struct mm_allocnode_s *)((char *)memory - SIZEOF_MM_ALLOCNODE);
		/* Corrupt only one allocated header; invoke the kernel's own checker. */
		node->size += 8;
		sched_lock();
		(void)mm_check_heap_corruption(kmm_get_heap(memory));
		PANIC();
	} else if (strcmp(argv[1], "histories") == 0) {
		int fd = open("/dev/sysdbg", O_WRONLY);
		if (fd < 0) {
			printf("CRASHPROBE sysdbg open failed\n");
			return -1;
		}
		(void)write(fd, "read", 4);
		(void)close(fd);
	} else {
		printf("CRASHPROBE unknown case\n");
		return -1;
	}
	printf("CRASHPROBE END %s\n", argv[1]);
	return 0;
}

void qemu_armv8m_register_crashprobe(void)
{
	(void)tash_cmd_install("crashprobe", crashprobe_main, TASH_EXECMD_SYNC);
}
