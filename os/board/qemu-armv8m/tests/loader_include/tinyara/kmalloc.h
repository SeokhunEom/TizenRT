#ifndef __TASK8_LOADER_KMALLOC_H
#define __TASK8_LOADER_KMALLOC_H

#include <stddef.h>

void *kmm_zalloc(size_t size);
void kmm_free(void *memory);

#endif
