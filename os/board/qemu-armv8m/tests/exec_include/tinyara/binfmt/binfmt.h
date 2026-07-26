#ifndef __TASK4_EXEC_TINYARA_BINFMT_BINFMT_H
#define __TASK4_EXEC_TINYARA_BINFMT_BINFMT_H

#include <stddef.h>
#include <stdint.h>
#include <tinyara/arch.h>

struct mm_heap_s;

enum {
	BIN_TEXT,
	BIN_CTOR,
	BIN_DTOR,
	BIN_RO,
	BIN_DATA,
	BIN_BSS,
	BIN_HEAP,
	BIN_MAX
};

struct binary_s {
	const char *filename;
	char *const *argv;
	main_t entrypt;
	uint32_t sections[BIN_MAX];
	size_t sizes[BIN_MAX];
	uint8_t priority;
	size_t stacksize;
	uint8_t binary_idx;
	uint32_t bin_ver;
	char *bin_name;
	struct mm_heap_s *uheap;
};

struct binfmt_s {
	struct binfmt_s *next;
	int (*load)(struct binary_s *bin);
	int (*unload)(struct binary_s *bin);
};

#endif
