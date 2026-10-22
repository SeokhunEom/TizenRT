/* SPDX-License-Identifier: Apache-2.0 */
/* TizenRT allocation policy and diagnostic metadata. No upstream node layout
 * is exposed here: the backend owns all splitting, coalescing and free lists.
 */
#include <tinyara/config.h>
#include <stdlib.h>
#include <sched.h>
#include <tinyara/mm/mm.h>
#include <tinyara/irq.h>
#include <tinyara/arch.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#include "bridge/nuttx_heap_bridge.h"
#include "../compat/pending_free.h"

/* Imported optional sources are retained, but these providers are not yet
 * integrated. Fail visibly instead of silently changing an enabled feature. */
#if defined(CONFIG_DEBUG_MM_UAF) || defined(CONFIG_DEBUG_MM_FREEINFO)
#error "NuttX MM port requires a TizenRT free-history/UAF provider for these debug options"
#endif
#ifdef CONFIG_MM_DUMP_CORRPUTED_HEAP
#error "NuttX MM requires a bounded fault-safe raw dump provider before MM_DUMP_CORRPUTED_HEAP"
#endif
#if defined(CONFIG_MM_ALLOC_LARGE_FROM_BACK)
#error "NuttX MM port has not implemented the TizenRT large-allocation placement extension"
#endif
#if defined(CONFIG_BINMGR_RECOVERY)
#error "NuttX MM requires force-release recovery hooks before BINMGR_RECOVERY can be enabled"
#endif
#if defined(CONFIG_GRAN) || defined(CONFIG_MM_PGALLOC) || defined(CONFIG_BUILD_KERNEL)
#error "NuttX MM port currently integrates flat/protected heaps, not granule/page/MMU heaps"
#endif

#define RECORD_MAGIC 0x544d4d31u
#define RECORD_PENDING 0x544d4d32u
#define RECORD_ALIGN 8u
struct mm_tizen_record_s {
	void *raw;
	struct mm_tizen_record_s *next;
	struct mm_tizen_record_s *prev;
	struct mm_tizen_record_s *pending;
	size_t requested;
	size_t charged;
	uint32_t magic;
	/* Must be last: legacy debug setters address this TizenRT-owned header. */
	struct mm_allocnode_s node;
};

static_assert(offsetof(struct mm_tizen_record_s, node) + sizeof(struct mm_allocnode_s) == sizeof(struct mm_tizen_record_s), "Tizen diagnostic header must immediately precede payload");
#ifdef CONFIG_BUILD_PROTECTED
/* User space cannot mask the IRQ producer of a shared app-heap free list.
 * Both images must use lock-free pointer atomics; a libatomic lock would be
 * unsafe if an interrupt preempted its owner. */
static_assert(__atomic_always_lock_free(sizeof(void *), 0), "Protected MM requires lock-free pointer atomics");
#endif

static struct mm_tizen_record_s *record_of(void *mem)
{
	return (struct mm_tizen_record_s *)((char *)mem - sizeof(struct mm_tizen_record_s));
}

size_t mm_allocation_charge(const void *mem)
{
    const struct mm_tizen_record_s *record;
    if (!mem) { return 0; }
    record = record_of((void *)mem);
    return record->magic == RECORD_MAGIC ? record->charged : 0;
}

const char *mm_nuttx_backend_version(void) { return "12.13.0"; }
const char *mm_nuttx_backend_revision(void) { return NUTTX_MM_UPSTREAM_REVISION; }

struct mm_allocnode_s *mm_first_allocnode(struct mm_heap_s *heap)
{
	return heap->allocations ? &heap->allocations->node : NULL;
}

struct mm_allocnode_s *mm_next_allocnode(struct mm_allocnode_s *node)
{
	struct mm_tizen_record_s *record = record_of((char *)node + SIZEOF_MM_ALLOCNODE);
	return record->next ? &record->next->node : NULL;
}

static void refresh_regions(struct mm_heap_s *heap)
{
	struct trmm_info info;
	unsigned int n = trmm_regions(heap->nuttx_heap);
	unsigned int i;
	for (i = 0; i < n && i < CONFIG_KMM_REGIONS; i++) {
		void *start;
		void *end;
		trmm_region(heap->nuttx_heap, i, &start, &end);
		heap->mm_heapstart[i] = start;
		heap->mm_heapend[i] = end;
	}
#if CONFIG_KMM_REGIONS > 1
	heap->mm_nregions = n;
#endif
	trmm_info(heap->nuttx_heap, &info);
	heap->mm_heapsize = info.arena;
}

#if defined(CONFIG_EXAMPLES_NUTTX_MM_LIFECYCLE) && defined(__KERNEL__)
static struct mm_lifecycle_stats_s g_mm_lifecycle;
void mm_lifecycle_snapshot(struct mm_lifecycle_stats_s *stats)
{
	irqstate_t flags = irqsave();
	*stats = g_mm_lifecycle;
	irqrestore(flags);
}
#endif

int mm_initialize(struct mm_heap_s *heap, void *start, size_t size)
{
	unsigned int i;
	memset(heap, 0, sizeof(*heap));
	mm_seminitialize(heap);
	heap->nuttx_heap = trmm_initialize("TizenRT", start, size);
	if (!heap->nuttx_heap) { sem_destroy(&heap->mm_semaphore); return -ENOMEM; }
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	for (i = 0; i < CONFIG_MAX_TASKS; i++) {
		heap->alloc_list[i].pid = HEAPINFO_INIT_INFO;
	}
#else
	(void)i;
#endif
	refresh_regions(heap);
#ifdef CONFIG_DEBUG_MM_HEAPINFO
    {
        struct trmm_info info;
        trmm_info(heap->nuttx_heap, &info);
        heap->total_alloc_size = info.uordblks;
        if (heap->peak_alloc_size < info.uordblks) { heap->peak_alloc_size = info.uordblks; }
    }
#endif
#if defined(CONFIG_EXAMPLES_NUTTX_MM_LIFECYCLE) && defined(__KERNEL__)
	__atomic_add_fetch(&g_mm_lifecycle.initialized, 1, __ATOMIC_RELAXED);
#endif
	return OK;
}

int mm_addregion(struct mm_heap_s *heap, void *start, size_t size)
{
	if (!heap || !heap->nuttx_heap || trmm_regions(heap->nuttx_heap) >= CONFIG_KMM_REGIONS) { return -EINVAL; }
	trmm_addregion(heap->nuttx_heap, start, size);
	refresh_regions(heap);
#ifdef CONFIG_DEBUG_MM_HEAPINFO
    {
        struct trmm_info info;
        trmm_info(heap->nuttx_heap, &info);
        heap->total_alloc_size = info.uordblks;
        if (heap->peak_alloc_size < info.uordblks) { heap->peak_alloc_size = info.uordblks; }
    }
#endif
	return OK;
}

static void release_record(struct mm_heap_s *heap, struct mm_tizen_record_s *record)
{
	DEBUGASSERT(record->magic == RECORD_MAGIC || record->magic == RECORD_PENDING);
	if (record->prev) { record->prev->next = record->next; }
	else { heap->allocations = record->next; }
	if (record->next) { record->next->prev = record->prev; }
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	heapinfo_subtract_size(heap, record->node.pid, record->node.size);
	heapinfo_update_total_size(heap, -record->charged, record->node.pid);
#endif
	record->magic = 0;
	trmm_free(heap->nuttx_heap, record->raw);
}

static void drain_pending(struct mm_heap_s *heap)
{
	struct mm_tizen_record_s *record;
#ifdef CONFIG_BUILD_PROTECTED
	/* An IRQ may enqueue while user space holds the metadata semaphore.
	 * A plain load followed by NULL would lose that intervening enqueue. */
	record = trmm_pending_take(&heap->pending_free);
#else
	irqstate_t flags = irqsave();
	record = heap->pending_free;
	heap->pending_free = NULL;
	irqrestore(flags);
#endif
	while (record) {
		struct mm_tizen_record_s *next = record->pending;
		release_record(heap, record);
		record = next;
	}
	trmm_drain(heap->nuttx_heap);
}

void mm_uninitialize(struct mm_heap_s *heap)
{
    int cancel_state;
    if (!heap || !heap->nuttx_heap) { return; }
    /* Caller has stopped all heap users; cancellation stays disabled until
     * both mutexes and queued frees have been removed from this RAM region. */
    if (!mm_takesemaphore(heap)) { PANIC(); for (;;) {} }
    if (heap->mm_counts_held != 1) { PANIC(); for (;;) {} }
    cancel_state = heap->mm_cancel_state;
    drain_pending(heap);
    trmm_uninitialize(heap->nuttx_heap);
    heap->nuttx_heap = NULL;
    heap->allocations = NULL;
    heap->pending_free = NULL;
    heap->mm_holder = -1;
    heap->mm_counts_held = 0;
    if (sem_destroy(&heap->mm_semaphore) != OK) { PANIC(); for (;;) {} }
#if defined(CONFIG_EXAMPLES_NUTTX_MM_LIFECYCLE) && defined(__KERNEL__)
    irqstate_t flags = irqsave();
    g_mm_lifecycle.uninitialized++;
    g_mm_lifecycle.last_uninitialized = heap;
    irqrestore(flags);
#endif
    if (task_setcancelstate(cancel_state, NULL) != OK) { PANIC(); for (;;) {} }
}

void *mm_memalign(struct mm_heap_s *heap, size_t alignment, size_t size, mmaddress_t caller)
{
	struct mm_tizen_record_s *record;
	void *raw;
	uintptr_t user;
	size_t total;
	if (alignment < RECORD_ALIGN) { alignment = RECORD_ALIGN; }
	if ((alignment & (alignment - 1)) || size > SIZE_MAX - sizeof(*record) - (alignment - 1)) { return NULL; }
	if (!heap || !heap->nuttx_heap || !mm_takesemaphore(heap)) { return NULL; }
	drain_pending(heap);
	total = size + sizeof(*record) + alignment - 1;
	raw = alignment > RECORD_ALIGN ? trmm_memalign(heap->nuttx_heap, alignment, total) : trmm_malloc(heap->nuttx_heap, total);
	if (!raw) { mm_givesemaphore(heap); return NULL; }
	user = ((uintptr_t)raw + sizeof(*record) + alignment - 1) & ~(uintptr_t)(alignment - 1);
	record = record_of((void *)user);
	memset(record, 0, sizeof(*record));
	record->raw = raw;
	record->requested = size;
	record->charged = trmm_usable_size(heap->nuttx_heap, raw) + trmm_node_overhead();
	record->magic = RECORD_MAGIC;
	record->node.preceding = MM_ALLOC_BIT;
	record->node.size = size + SIZEOF_MM_ALLOCNODE;
	record->next = heap->allocations;
	if (record->next) { record->next->prev = record; }
	heap->allocations = record;
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	heapinfo_update_node(&record->node, caller);
	heapinfo_add_size(heap, record->node.pid, record->node.size);
	heapinfo_update_total_size(heap, record->charged, record->node.pid);
#endif
	mm_givesemaphore(heap);
	return (void *)user;
}

void *mm_malloc(struct mm_heap_s *heap, size_t size, mmaddress_t caller)
{
	return mm_memalign(heap, RECORD_ALIGN, size, caller);
}

void *mm_zalloc(struct mm_heap_s *heap, size_t size, mmaddress_t caller)
{
	void *mem = mm_malloc(heap, size, caller);
	if (mem) { memset(mem, 0, size); }
	return mem;
}

void *mm_calloc(struct mm_heap_s *heap, size_t n, size_t size, mmaddress_t caller)
{
	if (size && n > SIZE_MAX / size) { return NULL; }
	return mm_zalloc(heap, n * size, caller);
}

void mm_free(struct mm_heap_s *heap, void *mem)
{
	struct mm_tizen_record_s *record;
	if (!mem) { return; }
	DEBUGASSERT(heap && heap->nuttx_heap);
	record = record_of(mem);
	DEBUGASSERT(record->magic == RECORD_MAGIC);
	if (!mm_takesemaphore(heap)) {
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
		record->magic = RECORD_PENDING;
#ifdef CONFIG_BUILD_PROTECTED
		trmm_pending_push(&heap->pending_free, record, pending);
#else
		irqstate_t flags = irqsave();
		record->pending = heap->pending_free;
		heap->pending_free = record;
		irqrestore(flags);
#endif
		return;
#else
        PANIC(); for (;;) {}
#endif
	}
	drain_pending(heap);
	release_record(heap, record);
	mm_givesemaphore(heap);
}

void mm_free_withinfo(struct mm_heap_s *heap, void *mem, mmaddress_t caller, pid_t pid)
{
	(void)caller;
	(void)pid;
	mm_free(heap, mem);
}

void *mm_realloc(struct mm_heap_s *heap, void *old, size_t size, mmaddress_t caller)
{
    struct mm_tizen_record_s *record;
    struct mm_tizen_record_s *previous;
    struct mm_tizen_record_s *next;
    void *raw;
    void *mem;
    size_t offset;
#ifdef CONFIG_DEBUG_MM_HEAPINFO
    size_t old_node_size;
    size_t old_charge;
    pid_t owner;
#endif
    if (!old) { return mm_malloc(heap, size, caller); }
    if (!size) { mm_free(heap, old); return NULL; }
    if (!mm_takesemaphore(heap)) { return NULL; }
    drain_pending(heap);
    record = record_of(old);
    DEBUGASSERT(record->magic == RECORD_MAGIC);
    offset = (char *)old - (char *)record->raw;
    if (size > SIZE_MAX - offset) { mm_givesemaphore(heap); return NULL; }
    previous = record->prev;
    next = record->next;
#ifdef CONFIG_DEBUG_MM_HEAPINFO
    old_node_size = record->node.size;
    old_charge = record->charged;
    owner = record->node.pid;
#endif
#ifdef CONFIG_REALLOC_DISABLE_NEIGHBOR_EXTENSION
    /* Existing TizenRT debugging policy: do not grow into a neighbor. */
    raw = trmm_malloc(heap->nuttx_heap, size + offset);
    if (raw) {
        size_t copy = record->requested < size ? record->requested : size;
        memcpy(raw, record->raw, offset + copy);
        trmm_free(heap->nuttx_heap, record->raw);
    }
#else
    raw = trmm_realloc(heap->nuttx_heap, record->raw, size + offset);
#endif
    if (!raw) { mm_givesemaphore(heap); return NULL; }
    mem = (char *)raw + offset;
    record = record_of(mem);
    record->raw = raw;
    record->requested = size;
    record->charged = trmm_usable_size(heap->nuttx_heap, raw) + trmm_node_overhead();
    record->node.size = size + SIZEOF_MM_ALLOCNODE;
    if (previous) { previous->next = record; }
    else { heap->allocations = record; }
    if (next) { next->prev = record; }
#ifdef CONFIG_DEBUG_MM_HEAPINFO
    heapinfo_subtract_size(heap, owner, old_node_size);
    heapinfo_add_size(heap, owner, record->node.size);
    heapinfo_update_total_size(heap, record->charged - old_charge, owner);
    record->node.alloc_call_addr = caller;
    record->node.memory_state = MM_MEMORY_STATE_UNUSED;
#endif
    mm_givesemaphore(heap);
    return mem;
}

void *mm_brkaddr(struct mm_heap_s *heap, int region)
{
	return trmm_brkaddr(heap->nuttx_heap, region);
}

void mm_extend(struct mm_heap_s *heap, void *mem, size_t size, int region)
{
	trmm_extend(heap->nuttx_heap, mem, size, region);
	refresh_regions(heap);
}

int mm_mallinfo(struct mm_heap_s *heap, struct mallinfo *info)
{
	struct trmm_info raw;
	int ret;
    if (!info) { return -EINVAL; }
    memset(info, 0, sizeof(*info));
    if (!heap || !heap->nuttx_heap) { return OK; }
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
    /* Fault-time statistics have no quiescent snapshot provider yet. Never
     * wait on a mutex held by the context that faulted. */
    if (abort_mode || up_interrupt_context()) { return -EAGAIN; }
#endif
	ret = trmm_info(heap->nuttx_heap, &raw);
	if (ret < 0) { return ret; }
	info->arena = raw.arena;
	info->ordblks = raw.ordblks;
	info->mxordblk = raw.mxordblk;
	info->uordblks = raw.uordblks;
	info->fordblks = raw.fordblks;
	return OK;
}

int mm_mallinfo_aligned(struct mm_heap_s *heap, struct mallinfo *info, size_t alignment)
{
	int ret = mm_mallinfo(heap, info);
	/* A conservative upper bound after Tizen metadata/alignment overhead. */
	size_t overhead = trmm_node_overhead() + sizeof(struct mm_tizen_record_s) + (alignment ? alignment - 1 : 0);
	if (ret == OK) { info->mxordblk = info->mxordblk > overhead ? info->mxordblk - overhead : 0; }
	return ret;
}

/* The caller either owns both locks or has established quiescence with IRQs
 * masked on the supported uniprocessor profile. Never log or allocate here. */
static int mm_check_records(struct mm_heap_s *heap)
{
    struct mm_tizen_record_s *record;
    struct mm_tizen_record_s *previous = NULL;
    size_t remaining = heap->mm_heapsize / sizeof(*record) + 1;
    for (record = heap->allocations; record; record = record->next) {
        uintptr_t address = (uintptr_t)record;
        if (!remaining-- || address % sizeof(uintptr_t) ||
            !trmm_heapmember(heap->nuttx_heap, record) ||
            !trmm_heapmember(heap->nuttx_heap, (void *)(address + sizeof(*record) - 1))) {
            return -EFAULT;
        }
        if ((record->magic != RECORD_MAGIC && record->magic != RECORD_PENDING) ||
            record->prev != previous || record->node.preceding != MM_ALLOC_BIT ||
            record->requested > heap->mm_heapsize ||
            record->node.size != record->requested + SIZEOF_MM_ALLOCNODE ||
            !trmm_heapmember(heap->nuttx_heap, record->raw)) {
            return -EFAULT;
        }
        previous = record;
    }
    return OK;
}

/* Diagnostics must not wait for a suspended heap owner. Unavailable is not
 * evidence of corruption, particularly when faulting inside an allocation. */
int mm_check_heap_corruption(struct mm_heap_s *heap)
{
    int ret;
    if (!heap || !heap->nuttx_heap) { return -EAGAIN; }
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
    if (abort_mode || up_interrupt_context()) {
        irqstate_t flags = irqsave();
        if (heap->mm_counts_held != 0 || heap->mm_semaphore.semcount != 1) {
            ret = -EAGAIN;
        } else {
            ret = trmm_check_quiescent(heap->nuttx_heap);
            if (ret == OK) { ret = mm_check_records(heap); }
        }
        irqrestore(flags);
        return ret;
    }
#endif
    if (mm_trysemaphore(heap) != OK) { return -EAGAIN; }
    ret = trmm_trylock(heap->nuttx_heap);
    if (ret != OK) {
        mm_givesemaphore(heap);
        return -EAGAIN;
    }
    ret = trmm_check(heap->nuttx_heap);
    if (ret == OK) { ret = mm_check_records(heap); }
    trmm_unlock(heap->nuttx_heap);
    mm_givesemaphore(heap);
    return ret;
}
