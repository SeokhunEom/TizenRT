/****************************************************************************
 * Capture the synchronous C boundary before checker workspace is created.
 ****************************************************************************/

#include <tinyara/arch.h>

	.syntax unified
	.arm
	.text
	.align 2
	.global up_mem_leak_capture_current
	.type up_mem_leak_capture_current, %function
up_mem_leak_capture_current:
	stmdb	sp!, {r0-r12, lr}
	ldr	r0, [sp, #0]
	ldr	r1, =UP_MEM_LEAK_CAPTURE_MAGIC
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_MAGIC_OFFSET]
	mov	r1, #UP_MEM_LEAK_CAPTURE_VERSION
	strh	r1, [r0, #UP_MEM_LEAK_CAPTURE_VERSION_OFFSET]
	mov	r1, #UP_MEM_LEAK_CAPTURE_WORDS
	strh	r1, [r0, #UP_MEM_LEAK_CAPTURE_WORDS_OFFSET]
	ldr	r1, [sp, #16]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 0)]
	ldr	r1, [sp, #20]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 4)]
	ldr	r1, [sp, #24]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 8)]
	ldr	r1, [sp, #28]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 12)]
	ldr	r1, [sp, #32]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 16)]
	ldr	r1, [sp, #36]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 20)]
	ldr	r1, [sp, #40]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 24)]
	ldr	r1, [sp, #44]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 28)]
	add	r1, sp, #56
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_SP_OFFSET]
	add	r2, r0, #UP_MEM_LEAK_CAPTURE_SIZE
	cmp	r1, r2
	movlo	r1, r2
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_BOUNDARY_OFFSET]
	mrs	r1, cpsr
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_STATUS_OFFSET]
	and	r2, r1, #0x1f
	cmp	r2, #0x10
	cmpne	r2, #0x13
	cmpne	r2, #0x1f
	moveq	r3, #0
	movne	r3, r2
	str	r3, [r0, #UP_MEM_LEAK_CAPTURE_EXCEPTION_OFFSET]
	movweq	r1, #(UP_MEM_LEAK_CAPTURE_FLAG_TASK | UP_MEM_LEAK_CAPTURE_FLAG_ARMV7_A)
	movne	r1, #UP_MEM_LEAK_CAPTURE_FLAG_EXCEPTION
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_FLAGS_OFFSET]
	mov	r1, #UP_MEM_LEAK_CAPTURE_CALLEE_MASK
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_MASK_OFFSET]
	bl	up_mem_leak_capture_identity
	ldmia	sp!, {r0-r12, lr}
	bx	lr
	.size up_mem_leak_capture_current, .-up_mem_leak_capture_current

	.global prctl
	.type prctl, %function
prctl:
	cmp	r0, #UP_MEM_LEAK_PRCTL_OPTION
	beq	.Lmem_leak_entry
	b	task_prctl_impl
	.size prctl, .-prctl

	.global run_all_mem_leak_checker
	.type run_all_mem_leak_checker, %function
run_all_mem_leak_checker:
	mov	r1, r0
	b	.Lmem_leak_entry
	.size run_all_mem_leak_checker, .-run_all_mem_leak_checker

.Lmem_leak_entry:
	stmdb	sp!, {r0-r12, lr}
	sub	sp, sp, #UP_MEM_LEAK_CAPTURE_SIZE
	mov	r0, sp
	ldr	r1, =UP_MEM_LEAK_CAPTURE_MAGIC
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_MAGIC_OFFSET]
	mov	r1, #UP_MEM_LEAK_CAPTURE_VERSION
	strh	r1, [r0, #UP_MEM_LEAK_CAPTURE_VERSION_OFFSET]
	mov	r1, #UP_MEM_LEAK_CAPTURE_WORDS
	strh	r1, [r0, #UP_MEM_LEAK_CAPTURE_WORDS_OFFSET]
	ldr	r1, [sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 16)]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 0)]
	ldr	r1, [sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 20)]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 4)]
	ldr	r1, [sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 24)]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 8)]
	ldr	r1, [sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 28)]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 12)]
	ldr	r1, [sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 32)]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 16)]
	ldr	r1, [sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 36)]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 20)]
	ldr	r1, [sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 40)]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 24)]
	ldr	r1, [sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 44)]
	str	r1, [r0, #(UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + 28)]
	add	r1, sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 56)
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_SP_OFFSET]
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_BOUNDARY_OFFSET]
	mrs	r1, cpsr
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_STATUS_OFFSET]
	and	r2, r1, #0x1f
	cmp	r2, #0x10
	cmpne	r2, #0x13
	cmpne	r2, #0x1f
	moveq	r3, #0
	movne	r3, r2
	str	r3, [r0, #UP_MEM_LEAK_CAPTURE_EXCEPTION_OFFSET]
	movweq	r1, #(UP_MEM_LEAK_CAPTURE_FLAG_TASK | UP_MEM_LEAK_CAPTURE_FLAG_ARMV7_A)
	movne	r1, #UP_MEM_LEAK_CAPTURE_FLAG_EXCEPTION
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_FLAGS_OFFSET]
	mov	r1, #UP_MEM_LEAK_CAPTURE_CALLEE_MASK
	str	r1, [r0, #UP_MEM_LEAK_CAPTURE_MASK_OFFSET]
	bl	up_mem_leak_capture_identity
	ldr	r0, [sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 4)]
	mov	r1, sp
	bl	run_all_mem_leak_checker_with_capture
	add	sp, sp, #(UP_MEM_LEAK_CAPTURE_SIZE + 4)
	ldmia	sp!, {r1-r12, lr}
	bx	lr
