# TizenRT policy around the synchronized allocator

`mm_api.c` preserves TizenRT's public `mm_*` signatures, caller address argument,
heap descriptors, and app/kernel/user routing. Its opaque `nuttx_heap` is the
only connection to the upstream heap. Allocation, splitting, coalescing,
alignment, and normal realloc are performed by NuttX 12.13.0 through the private
bridge. `CONFIG_REALLOC_DISABLE_NEIGHBOR_EXTENSION` retains the explicit TizenRT
alloc/copy/free debug policy.

Every allocation has a TizenRT-owned prefix. It records the underlying pointer,
linked allocation metadata, requested size, actual backend chunk charge, and
the caller/PID fields used by heapinfo and the leak checker. This is an
intentional product-layer memory overhead, not an upstream node layout or an
ABI cast. The `mm_allocnode_s` immediately before the user address is exclusively
this TizenRT metadata; `preceding` is an allocation marker, not a physical
neighbor size. Physical heap inspection uses `bridge/nuttx_heap_bridge.c`.
The compile-time layout assertion ensures there is no padding between the
metadata and its payload. Realloc repairs list neighbors after backend moves.

Heap region ends are exclusive. `mm_mallinfo` overwrites one heap snapshot;
`mallinfo` and `kmm_mallinfo` explicitly sum snapshots across their applicable
heaps and take the maximum physical free chunk. `mm_get_largest_freenode_size`
and `kmm_get_largest_freenode_size` use that same physical chunk convention.
`mm_mallinfo_aligned` reports a conservative capacity after alignment and both
metadata layers. Per-PID counters describe the Tizen allocation plus diagnostic
header; aggregate heap counters include the actual upstream chunk charge and
guard nodes. These are intentionally different quantities. Group heap charges use the
physical amount exposed by `mm_allocation_charge`, including PID transfers and
stack attribution removal; the existing separate group stack charge remains
`adj_stack_size`. Updating the owner PID transfers group/per-PID accounting
without changing the heap's aggregate allocated bytes.

The metadata semaphore is acquired before the backend mutex. Cancellation is
disabled for this complete interval; backend unlock inherits the disabled state
and only the outermost metadata unlock restores the original state. IRQ and
task-exit transition frees enqueue the Tizen prefix without acquiring either
lock; the next thread-context allocation/free drains this queue. The queue is
protected by native IRQ save/restore in flat builds. Protected kernel and user
images share lock-free pointer publication/detachment operations, so an IRQ
enqueue cannot be lost during a user-space drain. Protected user code does not
issue privileged IRQ instructions.

`mm_uninitialize` requires that the caller has stopped all heap users. It drains
queued frees, destroys both mutexes, and drops metadata references before app
RAM is released or reused. Binfmt orderly unload and load-failure paths use this
boundary. Force-release recovery is a different contract: `BINMGR_RECOVERY` is
rejected until resetting allocator owner/recursion state is integrated with
Binary Manager's semaphore recovery.

The initial runtime acceptance profile is flat QEMU ARMv8-M with multiheap and
heapinfo enabled. The protected `loadable_all` and `loadable_apps` profiles now
also exercise normal exit and reload: the original group and separate child
groups must all release their TCBs before deferred image/heap teardown. See the
[protected validation record](../../../docs/NuttX_12.13_MM_Loadable_Validation.md)
and [developer migration guide](../../../docs/NuttX_12.13_MM_Migration_Guide.md).
Live-app forced termination/update is outside this contract even when fault
recovery is disabled. Global `HEAPINFO_USER_GROUP` charges for live allocations
discarded by whole-heap teardown are not yet decremented; the lifecycle test
uses backend kernel heap totals with that option disabled. Free-history/UAF debugging, large-allocation
back placement, granule/page/MMU heaps, and Binary Manager force recovery fail
explicitly when enabled; none is silently compiled into a no-op. The imported
optional allocator sources remain available for later module work.

Fault-time corruption diagnostics never acquire a heap lock. On the supported
uniprocessor build they mask IRQs and inspect only when both the metadata lock
and backend mutex are idle. A busy/uninitialized heap returns `-EAGAIN` and ARM
assertion paths print that the check is unavailable, then continue the crash
report. Only `-EFAULT` is classified as confirmed corruption. Normal-context
checks use trylocks, so a diagnostic cannot wait forever on a stopped owner.
`MM_DUMP_CORRPUTED_HEAP` is rejected until a separate bounded raw dump provider
can guarantee safe fault-time reads; it is not a silent no-op.
