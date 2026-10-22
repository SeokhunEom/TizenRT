/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nuttx_heap_bridge.h"
extern _Thread_local bool trmm_test_irq;
extern _Thread_local bool trmm_test_switching;
static _Alignas(64) unsigned char memory[1024 * 1024];
static _Alignas(64) unsigned char extra[65536];
static void *heap;
static void *worker(void *arg)
{
  uintptr_t id = (uintptr_t)arg;
  for (unsigned i = 0; i < 4000; ++i)
    {
      size_t n = 1 + (i * 131 + id * 53) % 4096;
      unsigned char *p = trmm_malloc(heap, n);
      assert(p);
      memset(p, (int)id, n);
      unsigned char *q = trmm_realloc(heap, p, n + 257);
      assert(q);
      for (size_t k = 0; k < n; ++k) { assert(q[k] == id); }
      trmm_free(heap, q);
    }
  return NULL;
}
static void count_nodes(const struct trmm_node *node, void *arg)
{
  if (node->allocated && !node->guard) { ++*(unsigned *)arg; }
}
int main(void)
{
  struct trmm_info baseline, after;
  heap = trmm_initialize("host-test", memory, sizeof(memory));
  assert(heap);
  trmm_set_system_heaps(heap, NULL);
  assert(trmm_check(heap) == 0);
  assert(trmm_check_quiescent(heap) == 0 && !trmm_is_locked(heap));
  assert(trmm_lock(heap) == 0);
  assert(trmm_check_quiescent(heap) == -EAGAIN && trmm_is_locked(heap));
  trmm_unlock(heap);
  assert(trmm_info(heap, &baseline) == 0);
  assert(baseline.arena == baseline.fordblks + baseline.uordblks);
  unsigned char *p = trmm_calloc(heap, 59, 13);
  assert(p);
  for (unsigned i = 0; i < 59 * 13; ++i) { assert(p[i] == 0); }
  memset(p, 0x5a, 59 * 13);
  p = trmm_realloc(heap, p, 32000);
  assert(p);
  for (unsigned i = 0; i < 59 * 13; ++i) { assert(p[i] == 0x5a); }
  p = trmm_realloc(heap, p, 33);
  assert(p && p[32] == 0x5a);
  assert(trmm_usable_size(heap, p) >= 33);
  trmm_free(heap, p);
  for (size_t a = 8; a <= 4096; a *= 2)
    {
      p = trmm_memalign(heap, a, 113);
      assert(p && ((uintptr_t)p & (a - 1)) == 0);
      memset(p, 0x1d, 113);
      trmm_free(heap, p);
    }
  assert(trmm_calloc(heap, SIZE_MAX, 2) == NULL);
  assert(trmm_memalign(heap, 3, 40) == NULL);
  assert(trmm_malloc(heap, sizeof(memory) * 2) == NULL);
  p = trmm_malloc(heap, 4096);
  assert(trmm_lock(heap) == 0);
  trmm_test_irq = true;
  trmm_free(heap, p); /* Held mutex: interrupt free must enqueue. */
  trmm_test_irq = false;
  trmm_unlock(heap);
  trmm_drain(heap);
  p = trmm_malloc(heap, 8192);
  trmm_test_switching = true;
  trmm_free(heap, p); /* Task-exit transitional gettid also defers. */
  trmm_test_switching = false;
  trmm_drain(heap);
  assert(trmm_info(heap, &after) == 0 && after.fordblks == baseline.fordblks);
  /* Realloc into a preceding free chunk moves overlapping payload ranges.
   * NuttX 12.13.0 uses memcpy here; the private C-library provider maps it
   * to memmove without modifying the imported allocator source. */
  void *before = trmm_malloc(heap, 256);
  p = trmm_malloc(heap, 4096);
  void *after_block = trmm_malloc(heap, 128);
  assert(before && p && after_block);
  memset(p, 0x67, 4096);
  trmm_free(heap, before);
  unsigned char *moved = trmm_realloc(heap, p, 4300);
  assert(moved && moved < p);
  for (unsigned i = 0; i < 4096; ++i) { assert(moved[i] == 0x67); }
  trmm_free(heap, moved);
  trmm_free(heap, after_block);
  pthread_t threads[4];
  for (uintptr_t i = 0; i < 4; ++i) { assert(pthread_create(&threads[i], NULL, worker, (void *)(i + 1)) == 0); }
  for (unsigned i = 0; i < 4; ++i) { assert(pthread_join(threads[i], NULL) == 0); }
  assert(trmm_check(heap) == 0);
  assert(trmm_info(heap, &after) == 0 && after.fordblks == baseline.fordblks);
  unsigned count = 0;
  assert(trmm_walk(heap, count_nodes, &count) == 0 && count == 0);
  trmm_addregion(heap, extra, sizeof(extra));
  assert(trmm_regions(heap) == 2);
  void *start, *end;
  assert(trmm_region(heap, 1, &start, &end) == 0);
  assert(start >= (void *)extra && end <= (void *)(extra + sizeof(extra)));
  assert(trmm_check(heap) == 0);
  /* A corrupted guard must produce a failure without walking indefinitely. */
  assert(trmm_region(heap, 0, &start, &end) == 0);
  size_t *guard = start;
  size_t saved_guard = guard[1];
  guard[1] = 0;
  assert(trmm_lock(heap) == 0);
  assert(trmm_check_quiescent(heap) == -EAGAIN && trmm_is_locked(heap));
  trmm_unlock(heap);
  assert(trmm_check_quiescent(heap) == -EFAULT && !trmm_is_locked(heap));
  assert(trmm_check(heap) == -EFAULT);
  guard[1] = saved_guard;
  assert(trmm_check(heap) == 0 && trmm_check_quiescent(heap) == 0);
  trmm_uninitialize(heap);
  heap = trmm_initialize("extend-test", memory, sizeof(memory) / 2);
  assert(heap);
  trmm_extend(heap, trmm_brkaddr(heap, 0), sizeof(memory) / 2, 0);
  p = trmm_malloc(heap, 16384);
  assert(p && trmm_check(heap) == 0);
  assert(trmm_info(heap, &after) == 0);
  assert(after.usmblks <= after.arena);
  trmm_free(heap, p);
  trmm_uninitialize(heap);
  puts("PASS: upstream heap allocation/realloc/alignment/overflow/OOM, deferred free, 4-thread contention, multiregion/extend accounting, normal/quiescent corruption detection");
  return 0;
}
