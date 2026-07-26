#ifndef __TASK8_LOADER_SYMTAB_H
#define __TASK8_LOADER_SYMTAB_H

struct symtab_s {
	const char *name;
	void *value;
};

void exec_getsymtab(const struct symtab_s **symtab, int *nsymbols);

#endif
