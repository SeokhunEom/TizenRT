#ifndef __INCLUDE_TINYARA_MM_MM_ALLOC_PADDING_H
#define __INCLUDE_TINYARA_MM_MM_ALLOC_PADDING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MM_ALLOC_PADDING_MALLOC_BOUND(min_chunk, free_node_size) \
	(((min_chunk) - 1) + ((free_node_size) - 1))
#define MM_ALLOC_PADDING_MEMALIGN_BOUND(min_chunk, free_node_size) \
	MM_ALLOC_PADDING_MALLOC_BOUND(min_chunk, free_node_size)
#define MM_ALLOC_PADDING_REALLOC_BOUND(min_chunk, free_node_size) \
	(((min_chunk) - 1) + 2 * ((free_node_size) - 1))
#define MM_ALLOC_PADDING_BOUND(min_chunk, free_node_size) \
	MM_ALLOC_PADDING_REALLOC_BOUND(min_chunk, free_node_size)

static inline bool mm_size_multiply_checked(size_t left, size_t right, size_t *result)
{
	if (result == NULL || (right != 0 && left > SIZE_MAX / right)) {
		return false;
	}
	*result = left * right;
	return true;
}

static inline bool mm_alloc_padding_from_sizes(size_t capacity, size_t requested,
		uint16_t *padding)
{
	size_t difference;

	if (padding == NULL || requested > capacity) {
		return false;
	}

	difference = capacity - requested;
	if (difference > UINT16_MAX) {
		return false;
	}

	*padding = (uint16_t)difference;
	return true;
}

static inline bool mm_requested_size_from_padding(size_t capacity, uint16_t padding,
		size_t *requested)
{
	if (requested == NULL || (size_t)padding > capacity) {
		return false;
	}

	*requested = capacity - padding;
	return true;
}

#endif
