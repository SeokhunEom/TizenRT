#ifndef __MLC_TASK7_MM_H
#define __MLC_TASK7_MM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <tinyara/config.h>

typedef size_t mmsize_t;

#define FAR
#define MM_ALLOC_BIT ((mmsize_t)1 << (sizeof(mmsize_t) * 8 - 1))
#define MM_MIN_CHUNK 16

struct mm_allocnode_s {
	mmsize_t preceding;
	uintptr_t alloc_call_addr;
	pid_t pid;
	uint16_t alloc_padding;
	mmsize_t size;
};

#define SIZEOF_MM_ALLOCNODE sizeof(struct mm_allocnode_s)

static inline bool mm_allocnode_get_requested_size(
		const struct mm_allocnode_s *node, size_t *requested)
{
	size_t capacity;

	if (node == NULL || requested == NULL ||
		node->size < SIZEOF_MM_ALLOCNODE) {
		return false;
	}
	capacity = node->size - SIZEOF_MM_ALLOCNODE;
	if (node->alloc_padding > capacity) {
		return false;
	}
	*requested = capacity - node->alloc_padding;
	return true;
}

struct mm_heap_s {
	pid_t mm_holder;
	int mm_counts_held;
	struct mm_allocnode_s *mm_heapstart[CONFIG_KMM_REGIONS];
	struct mm_allocnode_s *mm_heapend[CONFIG_KMM_REGIONS];
};

#endif
