#ifndef __TASK8_LOADER_BINFMT_H
#define __TASK8_LOADER_BINFMT_H

#include <stddef.h>
#include <stdint.h>

struct symtab_s;

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
	uint32_t ramsize;
	const struct symtab_s *exports;
	int nexports;
	uint32_t sections[BIN_MAX];
	uint8_t priority;
	size_t stacksize;
	size_t filelen;
	size_t offset;
	uint8_t binary_idx;
	uint32_t bin_ver;
	char *bin_name;
	uint8_t islibrary;
};

extern struct binary_s *g_lib_binp;

int load_module(struct binary_s *bin);
int unload_module(struct binary_s *bin);
int exec_module(struct binary_s *bin);

#endif
