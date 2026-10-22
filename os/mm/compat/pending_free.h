/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_MM_PENDING_FREE_H
#define TIZENRT_MM_PENDING_FREE_H
#include <stddef.h>

/* The protected user allocator cannot mask a kernel IRQ which publishes a
 * deferred free to the same app heap. Use these operations in both images.
 * The caller must verify that pointer atomics are always lock-free. Nodes
 * remain owned by the producer until publication, and by the sole consumer
 * after detachment. Reclamation must not race another consumer. */
#define trmm_pending_take(head) \
  __atomic_exchange_n((head), NULL, __ATOMIC_ACQUIRE)

#define trmm_pending_push(head, node, link) do { \
  __typeof__(head) trmm_head_ = (head); \
  __typeof__(node) trmm_node_ = (node); \
  trmm_node_->link = __atomic_load_n(trmm_head_, __ATOMIC_RELAXED); \
  while (!__atomic_compare_exchange_n(trmm_head_, &trmm_node_->link, \
                                     trmm_node_, 0, __ATOMIC_RELEASE, \
                                     __ATOMIC_RELAXED)) { } \
} while (0)
#endif
