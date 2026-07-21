#ifndef __MM_MM_HEAP_MM_REALLOC_LOGIC_H
#define __MM_MM_HEAP_MM_REALLOC_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <tinyara/mm/mm_alloc_padding.h>

enum mm_realloc_branch_e {
	MM_REALLOC_BRANCH_SAME,
	MM_REALLOC_BRANCH_SHRINK,
	MM_REALLOC_BRANCH_PREVIOUS,
	MM_REALLOC_BRANCH_NEXT,
	MM_REALLOC_BRANCH_BOTH,
	MM_REALLOC_BRANCH_MOVE,
};

struct mm_realloc_plan_s {
	enum mm_realloc_branch_e branch;
	size_t take_previous;
	size_t take_next;
	size_t final_size;
};

static inline bool mm_size_add_checked(size_t left, size_t right, size_t *result)
{
	if (result == NULL || right > SIZE_MAX - left) {
		return false;
	}
	*result = left + right;
	return true;
}

static inline bool mm_realloc_plan(size_t old_size, size_t new_size,
		size_t previous_size, size_t next_size, size_t free_node_size,
		struct mm_realloc_plan_s *plan)
{
	size_t available;
	size_t needed;

	if (plan == NULL || new_size == 0 || old_size == 0 || free_node_size == 0) {
		return false;
	}
	plan->take_previous = 0;
	plan->take_next = 0;
	plan->final_size = old_size;
	if (new_size <= old_size) {
		plan->branch = new_size == old_size ? MM_REALLOC_BRANCH_SAME : MM_REALLOC_BRANCH_SHRINK;
		return true;
	}
	if (!mm_size_add_checked(old_size, previous_size, &available) ||
			!mm_size_add_checked(available, next_size, &available)) {
		return false;
	}
	if (available < new_size) {
		plan->branch = MM_REALLOC_BRANCH_MOVE;
		return true;
	}

	needed = new_size - old_size;
	if (previous_size > 0 && (next_size >= previous_size || next_size == 0)) {
		plan->take_previous = needed > previous_size ? previous_size : needed;
		plan->take_next = needed - plan->take_previous;
	} else {
		plan->take_next = needed > next_size ? next_size : needed;
		plan->take_previous = needed - plan->take_next;
	}
	if (plan->take_previous > 0 && previous_size - plan->take_previous < free_node_size) {
		plan->take_previous = previous_size;
	}
	if (plan->take_next > 0 && next_size - plan->take_next < free_node_size) {
		plan->take_next = next_size;
	}
	if (!mm_size_add_checked(old_size, plan->take_previous, &plan->final_size) ||
			!mm_size_add_checked(plan->final_size, plan->take_next, &plan->final_size) ||
			plan->final_size < new_size) {
		return false;
	}
	if (plan->take_previous > 0 && plan->take_next > 0) {
		plan->branch = MM_REALLOC_BRANCH_BOTH;
	} else if (plan->take_previous > 0) {
		plan->branch = MM_REALLOC_BRANCH_PREVIOUS;
	} else {
		plan->branch = MM_REALLOC_BRANCH_NEXT;
	}
	return true;
}

static inline size_t mm_realloc_copy_size(size_t old_payload_size, size_t requested_size)
{
	return old_payload_size < requested_size ? old_payload_size : requested_size;
}

static inline void mm_realloc_copy(void *destination, const void *source,
		size_t old_payload_size, size_t requested_size)
{
	memmove(destination, source, mm_realloc_copy_size(old_payload_size, requested_size));
}

static inline bool mm_realloc_publish_padding(size_t chunk_size, size_t header_size,
		size_t requested_size, uint16_t *padding)
{
	if (chunk_size < header_size) {
		return false;
	}
	return mm_alloc_padding_from_sizes(chunk_size - header_size, requested_size, padding);
}

#endif
