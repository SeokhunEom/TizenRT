#ifndef __MLC_TASK6_NATIVE_MM_H
#define __MLC_TASK6_NATIVE_MM_H

#include <stdbool.h>
#include <sys/types.h>
#include <semaphore.h>
#include <arch/irq.h>
#include <stdint.h>
#include <sched/sched.h>

typedef size_t mmsize_t;

#define MM_ALLOC_BIT 0x80000000

struct mm_allocnode_s {
	mmsize_t preceding;
	void *alloc_call_addr;
	pid_t pid;
	uint16_t alloc_padding;
	mmsize_t size;
};

#define SIZEOF_MM_ALLOCNODE (sizeof(mmsize_t) + sizeof(mmsize_t) + \
	sizeof(void *) + sizeof(pid_t) + sizeof(uint16_t))

struct mm_heap_s {
	sem_t mm_semaphore;
	pid_t mm_holder;
	int mm_counts_held;
	struct mm_allocnode_s *mm_heapstart[CONFIG_KMM_REGIONS];
	struct mm_allocnode_s *mm_heapend[CONFIG_KMM_REGIONS];
};

#define MM_LOADABLE_DOMAIN_NAME_MAX 32
#define MM_LOADABLE_DOMAIN_WRITABLE_MAX 2
#define MM_LOADABLE_DOMAIN_CAPACITY 8

struct mm_loadable_mapping_s {
	uintptr_t start;
	size_t size;
	uintptr_t container;
	size_t container_size;
};

typedef bool (*mm_loadable_domain_ready_t)(void *descriptor);

struct mm_loadable_domain_registration_s {
	unsigned int slot;
	struct mm_heap_s *heap;
	void *descriptor;
	void *descriptor_container;
	size_t descriptor_container_size;
	const char *name;
	mm_loadable_domain_ready_t ready;
	uintptr_t text_start;
	size_t text_size;
	struct mm_loadable_mapping_s writable[MM_LOADABLE_DOMAIN_WRITABLE_MAX];
	size_t writable_count;
};

struct mm_loadable_domain_pin_s {
	unsigned int slot;
	uint32_t generation;
	struct mm_heap_s *heap;
	void *descriptor;
	void *descriptor_container;
	size_t descriptor_container_size;
	char name[MM_LOADABLE_DOMAIN_NAME_MAX];
	uintptr_t text_start;
	size_t text_size;
	struct mm_loadable_mapping_s writable[MM_LOADABLE_DOMAIN_WRITABLE_MAX];
	size_t writable_count;
};

void mm_seminitialize(struct mm_heap_s *heap);
int mm_trysemaphore_fresh(struct mm_heap_s *heap);
bool mm_takesemaphore(struct mm_heap_s *heap);
void mm_givesemaphore(struct mm_heap_s *heap);
struct mm_heap_s *mm_get_heap(void *address);
struct mm_heap_s *kmm_get_baseheap(void);
int mm_initialize(struct mm_heap_s *heap, void *start, size_t size);
void mm_add_app_heap_list(struct mm_heap_s *heap, char *name);
void mm_remove_app_heap_list(struct mm_heap_s *heap);
void mm_disable_app_heap_list(struct mm_heap_s *heap);
void mm_loadable_domain_initialize(void);
int mm_loadable_domain_register(const struct mm_loadable_domain_registration_s *registration);
int mm_loadable_domain_activate(void *descriptor);
int mm_loadable_domain_abort(void *descriptor);
int mm_loadable_domain_disable_and_wait(void *descriptor);
int mm_loadable_domain_reactivate(void *descriptor);
int mm_loadable_domain_finish_unload(void *descriptor);
int mm_loadable_domain_try_pin_all(struct mm_loadable_domain_pin_s *pins,
		size_t capacity, size_t *count);
int mm_loadable_domain_unpin_all(const struct mm_loadable_domain_pin_s *pins,
		size_t count);

#endif
