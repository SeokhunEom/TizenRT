#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CAPTURE_MAGIC 0x4d4c4352u
#define CAPTURE_VERSION 1u
#define CAPTURE_WORDS 18u
#define CAPTURE_TASK 1u
#define CAPTURE_ARMV7_M (1u << 8)
#define CALLEE_MASK 0xffu

struct capture_s {
	uint32_t magic;
	uint16_t version;
	uint16_t words;
	uint32_t flags;
	uint32_t callee[8];
	uint32_t sp;
	uint32_t boundary;
	uint32_t status;
	uint32_t exception;
	uint32_t cpu;
	uint32_t tcb;
	uint32_t mask;
};

struct context_s {
	uint32_t cpu;
	uint32_t tcb;
	uint32_t stack_low;
	uint32_t stack_high;
	bool running;
};

enum saved_mode_e {
	SAVED_BLOCKED,
	SAVED_REMOTE_PAUSED,
	SAVED_IRQ
};

enum saved_arch_e {
	SAVED_ARMV7_A,
	SAVED_ARMV7_M
};

struct saved_record_s {
	uint32_t registers[18];
	uint32_t saved_sp;
};

struct saved_scan_result_s {
	bool incomplete;
	unsigned int rows;
	bool reusable;
};

static bool saved_status_valid(enum saved_arch_e architecture,
		enum saved_mode_e mode, const struct saved_record_s *record)
{
	if (architecture == SAVED_ARMV7_A) {
		uint32_t cpsr = record->registers[16];
		uint32_t processor_mode = cpsr & 0x1fu;

		if (processor_mode != 0x10u && processor_mode != 0x13u &&
			processor_mode != 0x1fu) {
			return false;
		}
		(void)mode;
		return (cpsr & 0x00f00000u) == 0;
	}
	return (record->registers[17] & (1u << 24)) != 0 &&
		(record->registers[17] & 0x1ffu) == 0;
}

static bool saved_valid(enum saved_mode_e mode, bool running,
		bool remote_paused, enum saved_arch_e architecture,
		const struct saved_record_s *record, const struct context_s *context)
{
	if (mode == SAVED_IRQ || !saved_status_valid(architecture, mode, record) ||
		(record->saved_sp & 7u) != 0 || record->saved_sp < context->stack_low ||
		record->saved_sp > context->stack_high) {
		return false;
	}
	if (running) {
		return mode == SAVED_REMOTE_PAUSED && remote_paused;
	}
	return mode == SAVED_BLOCKED && !remote_paused;
}

static struct saved_scan_result_s scan_saved_record(enum saved_mode_e mode,
		enum saved_arch_e architecture, const struct saved_record_s *record,
		const struct context_s *context)
{
	if (!saved_valid(mode, false, false, architecture, record, context)) {
		return (struct saved_scan_result_s){ true, 0, true };
	}
	return (struct saved_scan_result_s){ false, 1, false };
}

static void assert_saved_record_rejected(enum saved_arch_e architecture,
		const struct saved_record_s *record, const struct context_s *context)
{
	struct saved_scan_result_s result = scan_saved_record(SAVED_BLOCKED,
		architecture, record, context);

	assert(result.incomplete);
	assert(result.rows == 0);
	assert(result.reusable);
}

static bool valid(const struct capture_s *capture,
		const struct context_s *context)
{
	return capture->magic == CAPTURE_MAGIC &&
		capture->version == CAPTURE_VERSION &&
		capture->words == CAPTURE_WORDS &&
		capture->flags == (CAPTURE_TASK | CAPTURE_ARMV7_M) &&
		capture->status == 0 && capture->exception == 0 &&
		capture->mask == CALLEE_MASK && (capture->sp & 7u) == 0 &&
		(capture->boundary & 7u) == 0 && capture->boundary >= capture->sp &&
		capture->boundary <= context->stack_high &&
		capture->sp >= context->stack_low &&
		capture->sp <= context->stack_high && context->running &&
		capture->cpu == context->cpu && capture->tcb == context->tcb;
}

static struct capture_s fixture(void)
{
	struct capture_s capture;
	unsigned int index;

	memset(&capture, 0, sizeof(capture));
	capture.magic = CAPTURE_MAGIC;
	capture.version = CAPTURE_VERSION;
	capture.words = CAPTURE_WORDS;
	capture.flags = CAPTURE_TASK | CAPTURE_ARMV7_M;
	for (index = 0; index < 8; index++) {
		capture.callee[index] = 0x20001000u + index * 4u;
	}
	capture.sp = 0x20002000u;
	capture.boundary = capture.sp + 72u;
	capture.cpu = 1;
	capture.tcb = 0x20000100u;
	capture.mask = CALLEE_MASK;
	return capture;
}

int main(int argc, char **argv)
{
	const struct context_s context = {
		1, 0x20000100u, 0x20001000u, 0x20003000u, true
	};
	struct capture_s capture = fixture();
	struct saved_record_s arm_a = { .saved_sp = 0x20002000u };
	struct saved_record_s arm_m = { .saved_sp = 0x20002000u };
	unsigned int index;
	bool happy;

	if (argc != 2) {
		return 64;
	}
	happy = strcmp(argv[1], "mlc_task_roots") == 0;
	if (!happy && strcmp(argv[1], "mlc_invalid_task_irq_context") != 0) {
		return 64;
	}

	arm_a.registers[16] = 0x13u;
	arm_m.registers[17] = 1u << 24;

	if (happy) {
		assert(valid(&capture, &context));
		for (index = 0; index < 8; index++) {
			assert(capture.callee[index] == 0x20001000u + index * 4u);
		}
		assert(saved_valid(SAVED_BLOCKED, false, false, SAVED_ARMV7_A,
			&arm_a, &context));
		assert(saved_valid(SAVED_BLOCKED, false, false, SAVED_ARMV7_M,
			&arm_m, &context));
		assert(saved_valid(SAVED_REMOTE_PAUSED, true, true, SAVED_ARMV7_A,
			&arm_a, &context));
		printf("MLC_TASK8_MODEL status=PASS roots=callee_saved,stack "
			"saved_context=blocked,remote_paused architectures=armv7a,armv7m\n");
		return 0;
	}

	capture.cpu = 0;
	assert(!valid(&capture, &context));
	capture = fixture();
	capture.tcb++;
	assert(!valid(&capture, &context));
	capture = fixture();
	capture.exception = 3;
	assert(!valid(&capture, &context));
	capture = fixture();
	capture.flags = 0;
	assert(!valid(&capture, &context));
	capture = fixture();
	capture.status = 1;
	assert(!valid(&capture, &context));
	capture = fixture();
	capture.sp = context.stack_low - 8;
	capture.boundary = capture.sp + 72u;
	assert(!valid(&capture, &context));
	capture = fixture();
	capture.mask ^= 1u;
	assert(!valid(&capture, &context));

	assert(valid(&(struct capture_s){
		.magic = CAPTURE_MAGIC, .version = CAPTURE_VERSION,
		.words = CAPTURE_WORDS, .flags = CAPTURE_TASK | CAPTURE_ARMV7_M,
		.sp = 0x20002000u, .boundary = 0x20002048u,
		.cpu = 1, .tcb = 0x20000100u, .mask = CALLEE_MASK
	}, &context));
	assert(!saved_valid(SAVED_REMOTE_PAUSED, true, false, SAVED_ARMV7_A,
		&arm_a, &context));
	assert(!saved_valid(SAVED_BLOCKED, true, false, SAVED_ARMV7_A,
		&arm_a, &context));
	assert(!saved_valid(SAVED_IRQ, false, false, SAVED_ARMV7_A,
		&arm_a, &context));
	arm_a.saved_sp = 0x20000ff8u;
	assert(!saved_valid(SAVED_BLOCKED, false, false, SAVED_ARMV7_A,
		&arm_a, &context));
	arm_a.saved_sp = 0x20002000u;
	arm_a.registers[16] = 0x11u;
	assert_saved_record_rejected(SAVED_ARMV7_A, &arm_a, &context);
	arm_a.registers[16] = 0x12u;
	assert_saved_record_rejected(SAVED_ARMV7_A, &arm_a, &context);
	arm_a.registers[16] = 0x13u | 0x00f00000u;
	assert_saved_record_rejected(SAVED_ARMV7_A, &arm_a, &context);
	arm_a.registers[16] = 0x13u | 0xc0u;
	assert(saved_valid(SAVED_BLOCKED, false, false, SAVED_ARMV7_A,
		&arm_a, &context));
	arm_m.registers[17] = 0;
	assert_saved_record_rejected(SAVED_ARMV7_M, &arm_m, &context);
	arm_m.registers[17] = (1u << 24) | 3u;
	assert_saved_record_rejected(SAVED_ARMV7_M, &arm_m, &context);

	printf("MLC_TASK8_FAILURES model_status=PASS expected_incomplete="
		"TASK_CONTEXT expected_rows=0 mutations=migration,tcb,irq,mode,sp,mask,cpsr,xpsr "
		"reuse=valid_after_rejection\n");
	return 0;
}
