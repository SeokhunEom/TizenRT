/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include "../pending_free.h"

#define PRODUCERS 4
#define COUNT 20000
struct pending_node {
  struct pending_node *next;
  unsigned id;
};
static struct pending_node nodes[PRODUCERS][COUNT];
static struct pending_node *head;
static unsigned char seen[PRODUCERS * COUNT];
static atomic_bool start;
static atomic_uint finished;

static void *publish(void *arg)
{
  unsigned producer = (uintptr_t)arg;
  while (!atomic_load_explicit(&start, memory_order_acquire)) { sched_yield(); }
  for (unsigned i = 0; i < COUNT; ++i) {
    struct pending_node *node = &nodes[producer][i];
    node->id = producer * COUNT + i;
    trmm_pending_push(&head, node, next);
  }
  atomic_fetch_add_explicit(&finished, 1, memory_order_release);
  return NULL;
}

int main(void)
{
  pthread_t threads[PRODUCERS];
  unsigned received = 0;
  _Static_assert(__atomic_always_lock_free(sizeof(void *), 0), "Lock-free pointers required");
  for (uintptr_t i = 0; i < PRODUCERS; ++i) {
    assert(pthread_create(&threads[i], NULL, publish, (void *)i) == 0);
  }
  atomic_store_explicit(&start, 1, memory_order_release);
  do {
    struct pending_node *node = trmm_pending_take(&head);
    while (node) {
      assert(node->id < PRODUCERS * COUNT && !seen[node->id]);
      seen[node->id] = 1;
      ++received;
      node = node->next;
    }
  } while (atomic_load_explicit(&finished, memory_order_acquire) != PRODUCERS ||
           __atomic_load_n(&head, __ATOMIC_ACQUIRE) != NULL);
  for (unsigned i = 0; i < PRODUCERS; ++i) { assert(pthread_join(threads[i], NULL) == 0); }
  assert(received == PRODUCERS * COUNT && trmm_pending_take(&head) == NULL);
  puts("PASS: protected pending-free publication/drain, 80000 nodes exactly once");
  return 0;
}
