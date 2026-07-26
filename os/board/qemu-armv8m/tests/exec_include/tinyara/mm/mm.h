#ifndef __TASK4_EXEC_TINYARA_MM_MM_H
#define __TASK4_EXEC_TINYARA_MM_MM_H

#include <stddef.h>

struct mm_heap_s {
	unsigned char reserved[32];
};

int mm_initialize(struct mm_heap_s *heap, void *heap_start, size_t heap_size);
void mm_add_app_heap_list(struct mm_heap_s *heap, char *app_name);
void mm_remove_app_heap_list(struct mm_heap_s *heap);

#endif
