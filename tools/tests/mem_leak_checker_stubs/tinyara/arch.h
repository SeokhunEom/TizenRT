#ifndef __TEST_TINYARA_ARCH_H
#define __TEST_TINYARA_ARCH_H

#include <stdint.h>

#define UP_MEM_LEAK_CAPTURE_MAGIC 0x4d4c4352u
#define UP_MEM_LEAK_CAPTURE_VERSION 1
#define UP_MEM_LEAK_CAPTURE_WORDS 18
#define UP_MEM_LEAK_CAPTURE_FLAG_TASK 1
#define UP_MEM_LEAK_CAPTURE_FLAG_EXCEPTION 2
#define UP_MEM_LEAK_CAPTURE_FLAG_ARMV7_M (1 << 8)
#define UP_MEM_LEAK_CAPTURE_FLAG_ARMV7_A (1 << 9)
#define UP_MEM_LEAK_CAPTURE_CALLEE_MASK 0xff
#define UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET 12
#define UP_MEM_LEAK_CAPTURE_SP_OFFSET 44
#define UP_MEM_LEAK_CAPTURE_BOUNDARY_OFFSET 48
#define UP_MEM_LEAK_CAPTURE_STATUS_OFFSET 52
#define UP_MEM_LEAK_CAPTURE_EXCEPTION_OFFSET 56
#define UP_MEM_LEAK_CAPTURE_CPU_OFFSET 60
#define UP_MEM_LEAK_CAPTURE_TCB_OFFSET 64
#define UP_MEM_LEAK_CAPTURE_MASK_OFFSET 68
#define UP_MEM_LEAK_CAPTURE_SIZE 72

#ifdef CONFIG_ARCH_ARMV7A_FAMILY
#define REG_SP 13
#define REG_CPSR 16
#define XCPTCONTEXT_REGS 17
#else
#define REG_SP 0
#define REG_XPSR 1
#define XCPTCONTEXT_REGS 2
#endif

struct up_mem_leak_capture_s {
	uint32_t magic;
	uint16_t version;
	uint16_t words;
	uint32_t flags;
	uint32_t callee_saved[8];
	uint32_t stack_pointer;
	uint32_t caller_boundary;
	uint32_t status;
	uint32_t exception;
	uint32_t cpu;
	uint32_t tcb;
	uint32_t callee_saved_mask;
};

#endif
