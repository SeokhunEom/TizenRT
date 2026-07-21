#ifndef __MLC_TASK6_NATIVE_ARCH_H
#define __MLC_TASK6_NATIVE_ARCH_H

#include <stdint.h>
#include <arch/irq.h>

#define UP_MEM_LEAK_CAPTURE_MAGIC       0x4d4c4352u
#define UP_MEM_LEAK_CAPTURE_VERSION     1
#define UP_MEM_LEAK_CAPTURE_WORDS       8
#define UP_MEM_LEAK_CAPTURE_FLAG_TASK   (1u << 0)
#define UP_MEM_LEAK_CAPTURE_FLAG_ARMV7_A (1u << 1)
#define UP_MEM_LEAK_CAPTURE_CALLEE_MASK 0xff

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

struct tcb_s;
void up_unblock_task(struct tcb_s *tcb);

#endif
