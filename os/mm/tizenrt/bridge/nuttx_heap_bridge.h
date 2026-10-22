/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_NUTTX_HEAP_BRIDGE_H
#define TIZENRT_NUTTX_HEAP_BRIDGE_H
#include <stddef.h>
#include <stdbool.h>
#define NUTTX_MM_UPSTREAM_REVISION "b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35"
struct trmm_info {
  size_t arena, ordblks, aordblks, mxordblk, uordblks, fordblks, usmblks;
};
struct trmm_node {
  void *address;
  size_t size;
  size_t chunk_size;
  bool allocated;
  bool guard;
  int pid;
};
typedef void (*trmm_visitor_t)(const struct trmm_node *, void *);
void *trmm_initialize(const char *name, void *start, size_t size);
void trmm_uninitialize(void *heap);
void trmm_addregion(void *heap, void *start, size_t size);
void *trmm_malloc(void *heap, size_t size);
void *trmm_zalloc(void *heap, size_t size);
void *trmm_calloc(void *heap, size_t count, size_t size);
void *trmm_realloc(void *heap, void *ptr, size_t size);
void *trmm_memalign(void *heap, size_t alignment, size_t size);
void trmm_free(void *heap, void *ptr);
size_t trmm_usable_size(void *heap, void *ptr);
size_t trmm_node_overhead(void);
bool trmm_heapmember(void *heap, void *ptr);
void *trmm_brkaddr(void *heap, int region);
void trmm_extend(void *heap, void *mem, size_t size, int region);
int trmm_lock(void *heap);
int trmm_trylock(void *heap);
void trmm_unlock(void *heap);
bool trmm_is_locked(void *heap);
int trmm_info(void *heap, struct trmm_info *info);
size_t trmm_regions(void *heap);
int trmm_region(void *heap, int region, void **start, void **end);
/* Callback executes under the heap lock. It must not change heap nodes. */
int trmm_walk(void *heap, trmm_visitor_t visitor, void *arg);
void trmm_drain(void *heap);
int trmm_check(void *heap);
/* UP fault diagnostics only: caller must disable interrupts and establish
 * that the Tizen metadata lock is idle. Does not acquire either heap lock or
 * mutate the allocator. Returns -EAGAIN if the backend mutex is held. */
int trmm_check_quiescent(void *heap);
/* Optional OOM diagnostics use these identities; no allocation policy here. */
void trmm_set_system_heaps(void *user_heap, void *kernel_heap);
#endif
