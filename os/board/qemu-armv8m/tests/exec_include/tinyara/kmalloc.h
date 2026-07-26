#ifndef __TASK4_EXEC_TINYARA_KMALLOC_H
#define __TASK4_EXEC_TINYARA_KMALLOC_H

#include <stddef.h>

void *kmm_zalloc(size_t size);
void kmm_free(void *memory);
void kumm_free(void *memory);

#endif
