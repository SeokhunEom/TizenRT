/* SPDX-License-Identifier: Apache-2.0 */
/* The only TizenRT translation unit permitted to inspect NuttX heap nodes.
 * Build with compat/symbol_map.h and the same flags as imported mm_heap. */
#include <nuttx/config.h>
#include <errno.h>
#include <nuttx/mm/mm.h>
#include "mm_heap/mm.h"
#include "nuttx_heap_bridge.h"

struct mm_heap_s *g_mmheap;
struct mm_heap_s *g_kmmheap;
static const struct trmm_userspace_data_s trmm_user_data = { &g_mmheap };
const struct trmm_userspace_s trmm_userspace = { &trmm_user_data };

void trmm_set_system_heaps(void *user_heap, void *kernel_heap)
{
  g_mmheap = user_heap;
  g_kmmheap = kernel_heap;
}

void *trmm_initialize(const char *name, void *start, size_t size)
{
  /* Upstream expects enough room for the heap header and guard/free nodes. */
  if (!start || size < sizeof(struct mm_heap_s) + 2 * MM_SIZEOF_ALLOCNODE +
                       MM_MIN_CHUNK + 2 * MM_ALIGN ||
      (uintptr_t)start > UINTPTR_MAX - size)
    {
      return NULL;
    }
  return mm_initialize(name, start, size);
}
void trmm_uninitialize(void *heap) { mm_uninitialize(heap); }
void trmm_addregion(void *heap, void *start, size_t size)
{
  mm_addregion(heap, start, size);
}
void *trmm_malloc(void *heap, size_t size) { return mm_malloc(heap, size); }
void *trmm_zalloc(void *heap, size_t size) { return mm_zalloc(heap, size); }
void *trmm_calloc(void *heap, size_t count, size_t size)
{
  if (size && count > SIZE_MAX / size) { return NULL; }
  return mm_calloc(heap, count, size);
}
void *trmm_realloc(void *heap, void *ptr, size_t size)
{
  return mm_realloc(heap, ptr, size);
}
void *trmm_memalign(void *heap, size_t alignment, size_t size)
{
  if (!alignment || (alignment & (alignment - 1)) ||
      size > SIZE_MAX - alignment - MM_MIN_CHUNK)
    {
      return NULL;
    }
  return mm_memalign(heap, alignment, size);
}
void trmm_free(void *heap, void *ptr) { mm_free(heap, ptr); }
size_t trmm_usable_size(void *heap, void *ptr)
{
  return ptr ? mm_malloc_size(heap, ptr) : 0;
}
size_t trmm_node_overhead(void) { return MM_ALLOCNODE_OVERHEAD; }
bool trmm_heapmember(void *heap, void *ptr)
{
  return mm_heapmember(heap, ptr);
}
void *trmm_brkaddr(void *heap, int region) { return mm_brkaddr(heap, region); }
void trmm_extend(void *opaque, void *mem, size_t size, int region)
{
  struct mm_heap_s *heap = opaque;
  /* v12.13.0 extends the allocated tail guard and frees that synthetic
   * block without first accounting for it. Credit the temporary block so
   * mm_free does not underflow mm_curused; no upstream source patch needed. */
  if (mm_lock(heap) < 0) { PANIC(); for (;;) {} }
  heap->mm_curused += size;
  mm_extend(heap, mem, size, region);
  mm_unlock(heap);
}
int trmm_lock(void *heap) { return mm_lock(heap); }
int trmm_trylock(void *opaque)
{
  struct mm_heap_s *heap = opaque;
  if (up_interrupt_context()) { return -EAGAIN; }
  if (_SCHED_GETTID() < 0) { return -ESRCH; }
  return nxmutex_trylock(&heap->mm_lock);
}
void trmm_unlock(void *heap) { mm_unlock(heap); }
bool trmm_is_locked(void *opaque)
{
  struct mm_heap_s *heap = opaque;
  return nxmutex_is_locked(&heap->mm_lock);
}
int trmm_info(void *heap, struct trmm_info *out)
{
  struct mallinfo info;
  if (!heap || !out) { return -EINVAL; }
  if (trmm_lock(heap) < 0) { return -EAGAIN; }
  info = mm_mallinfo(heap);
  out->arena = info.arena;
  out->ordblks = info.ordblks;
  out->aordblks = info.aordblks;
  out->mxordblk = info.mxordblk;
  out->uordblks = info.uordblks;
  out->fordblks = info.fordblks;
  out->usmblks = info.usmblks;
  trmm_unlock(heap);
  return 0;
}
size_t trmm_regions(void *opaque)
{
  struct mm_heap_s *heap = opaque;
#if CONFIG_MM_REGIONS > 1
  return heap->mm_nregions;
#else
  return heap ? 1 : 0;
#endif
}
int trmm_region(void *opaque, int region, void **start, void **end)
{
  struct mm_heap_s *heap = opaque;
  if (!heap || region < 0 || (size_t)region >= trmm_regions(heap))
    { return -EINVAL; }
  if (start) { *start = heap->mm_heapstart[region]; }
  if (end) { *end = (char *)heap->mm_heapend[region] + MM_SIZEOF_ALLOCNODE; }
  return 0;
}
struct trmm_walk_s {
  struct mm_heap_s *heap;
  trmm_visitor_t visit;
  void *arg;
};
static void trmm_visit_node(struct mm_allocnode_s *node, void *opaque)
{
  struct trmm_walk_s *walk = opaque;
  struct trmm_node view;
  size_t i;
  view.guard = false;
  for (i = 0; i < trmm_regions(walk->heap); ++i)
    {
      if (node == walk->heap->mm_heapstart[i] ||
          node == walk->heap->mm_heapend[i])
        { view.guard = true; break; }
    }
  view.address = (char *)node + MM_SIZEOF_ALLOCNODE;
  view.chunk_size = MM_SIZEOF_NODE(node);
  view.size = view.guard ? 0 : view.chunk_size - MM_ALLOCNODE_OVERHEAD;
  view.allocated = MM_NODE_IS_ALLOC(node);
  view.pid = -1;
  walk->visit(&view, walk->arg);
}
int trmm_walk(void *heap, trmm_visitor_t visitor, void *arg)
{
  struct trmm_walk_s walk = { heap, visitor, arg };
  int ret;
  if (!heap || !visitor) { return -EINVAL; }
  ret = trmm_lock(heap);
  if (ret < 0) { return ret; }
  mm_foreach(heap, trmm_visit_node, &walk);
  trmm_unlock(heap);
  return 0;
}
void trmm_drain(void *heap) { mm_free_delaylist(heap); }
/* Validate bounded node walks before invoking upstream diagnostic walkers.
 * A corrupted size must never turn a debug query into an unbounded traversal. */
static bool trmm_valid_freenode(struct mm_heap_s *heap,
                                struct mm_freenode_s *candidate)
{
  size_t r;
  uintptr_t address = (uintptr_t)candidate;
  uintptr_t first = (uintptr_t)&heap->mm_nodelist[0];
  uintptr_t last = (uintptr_t)&heap->mm_nodelist[MM_NNODES];
  if (address >= first && address < last)
    { return (address - first) % sizeof(*candidate) == 0; }
  for (r = 0; r < trmm_regions(heap); ++r)
    {
      struct mm_allocnode_s *node = heap->mm_heapstart[r];
      uintptr_t end = (uintptr_t)heap->mm_heapend[r];
      while ((uintptr_t)node < end)
        {
          size_t size = MM_SIZEOF_NODE(node);
          if ((uintptr_t)node == address)
            { return MM_NODE_IS_FREE(node); }
          if (size < MM_SIZEOF_ALLOCNODE || size > end - (uintptr_t)node)
            { break; }
          node = (void *)((char *)node + size);
        }
    }
  return false;
}

/* Caller either owns the backend mutex or has stopped UP execution with
 * interrupts disabled and verified that neither heap lock is held. */
static int trmm_check_nodes(struct mm_heap_s *heap)
{
  size_t region;
  int ret = 0;
  for (region = 0; region < trmm_regions(heap); ++region)
    {
      struct mm_allocnode_s *node = heap->mm_heapstart[region];
      struct mm_allocnode_s *prev = NULL;
      uintptr_t end = (uintptr_t)heap->mm_heapend[region];
      if (!node || (uintptr_t)node >= end) { ret = -EFAULT; break; }
      for (;;)
        {
          size_t size = MM_SIZEOF_NODE(node);
          bool guard = node == heap->mm_heapstart[region] || (uintptr_t)node == end;
          if (size < MM_SIZEOF_ALLOCNODE || (!guard && size % MM_ALIGN != 0) ||
              (guard && (size != MM_SIZEOF_ALLOCNODE || MM_NODE_IS_FREE(node))) ||
              (MM_PREVNODE_IS_FREE(node) &&
               (!prev || MM_NODE_IS_ALLOC(prev) || node->preceding != MM_SIZEOF_NODE(prev))) ||
              (prev && MM_PREVNODE_IS_ALLOC(node) && MM_NODE_IS_FREE(prev)))
            { ret = -EFAULT; break; }
          if ((uintptr_t)node == end) { break; }
          if (size > end - (uintptr_t)node)
            { ret = -EFAULT; break; }
          if (MM_NODE_IS_FREE(node))
            {
              struct mm_freenode_s *free = (void *)node;
              if (size < MM_MIN_CHUNK || !trmm_valid_freenode(heap, free->blink) ||
                  free->blink->flink != free ||
                  (free->flink && (!trmm_valid_freenode(heap, free->flink) ||
                                    free->flink->blink != free)))
                { ret = -EFAULT; break; }
            }
          prev = node;
          node = (void *)((char *)node + size);
        }
      if (ret < 0) { break; }
    }
  return ret;
}

int trmm_check(void *opaque)
{
  struct mm_heap_s *heap = opaque;
  int ret;
  if (!heap) { return -EINVAL; }
  ret = trmm_lock(heap);
  if (ret < 0) { return ret; }
  ret = trmm_check_nodes(heap);
  trmm_unlock(heap);
  return ret;
}

int trmm_check_quiescent(void *opaque)
{
  struct mm_heap_s *heap = opaque;
  if (!heap) { return -EINVAL; }
  /* A fault can interrupt an incomplete allocator mutation. Even the faulting
   * task's own mutex must be idle before interpreting the node invariants.
   * Merely observe it: no lock, cancellation, logging or delayed-free work. */
  if (nxmutex_is_locked(&heap->mm_lock)) { return -EAGAIN; }
  return trmm_check_nodes(heap);
}
