# Temporary NuttX MM providers

These headers are private to the imported `mm_heap` translation units and
`tizenrt/bridge/nuttx_heap_bridge.c`. Do not put `compat/include` on the global
TizenRT include path. Force-include `symbol_map.h` only for those files;
`nuttx_os.c` uses ordinary TizenRT names and must not receive that mapping.

The allocator source is NuttX `nuttx-12.13.0`, commit
`b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35`. The existing TizenRT Make build selects
the default heap implementation. The imported CMake files remain provenance
material and are not used by this port.

| Provider | Contract supplied now | Replacement/removal condition |
| --- | --- | --- |
| `nuttx_os.c`, `nuttx/mutex.h` | Recursive Tizen semaphore mutex, negative errno results, EINTR retry, caller errno preserved. Cancellation disabled from first acquisition until final unlock. | A native NuttX mutex provider with the same lifetime and Tizen caller contract. |
| `nuttx_os.c`, `nuttx/arch.h` | UP interrupt detection and critical-section save/restore. Protected userspace never executes privileged IRQ instructions. | A shared architecture/IRQ compatibility module or migrated architecture APIs. |
| `nuttx_os.c`, `nuttx/sched.h` | Boot ID 0; `-ESRCH` when the ready head is not RUNNING during `task_exit`, allowing upstream deferred free. Protected userspace gets its PID by syscall. | Migrated scheduler with native NuttX task identity and exit-transition semantics. |
| `nuttx/config.h` | Explicit default-heap profile: KMM region capacity, 8-byte alignment, no NuttX mempool/KASAN/backtrace. | Per-feature providers and their verification before enabling the features. |
| `malloc.h` | Private NuttX mallinfo layout, isolated from TizenRT's different public layout. | Shared public ABI or removal of the Tizen wrapper. |
| `nuttx/userspace.h` | Private backend heap identities. Native Tizen policy owns allocation failure; NuttX dump/panic-on-failure options are rejected because native identities are not registered. | Migrated userspace ABI or upstream abstraction for heap identity. |
| `nuttx/fs/procfs.h`, `nuttx/sched_note.h` | Disabled NuttX procfs registration and disabled heap tracing. Tizen diagnostics live under `tizenrt/`. Enabling NuttX heap trace is a compile error. | Actual procfs/trace adapters; these are not no-op implementations of enabled features. |
| `symbol_map.h` | Private `nxmm_*` symbols; overlap-safe `memcpy` provider for imported files. | Shared symbols after old ABI removal; remove copy override only after upstream's realloc assumption is fixed/verified. |

## Realloc copy assumption

NuttX 12.13.0 `mm_heap/mm_realloc.c:401-405` moves an allocation downward into a
preceding free chunk using `memcpy`, with a comment that it should be safe.
Those ranges can overlap. The host ASan regression reported
`memcpy-param-overlap` in that path. ISO C does not promise that behavior.
The private symbol projection supplies `memmove` for `memcpy` in imported heap
files, after including the C library declarations. This leaves upstream bytes
unchanged and gives both host and target builds defined overlapping-copy
semantics. The test first allocates 256/4096/128 bytes, frees the first block,
then grows the middle block to 4300 bytes and verifies every preserved byte.

## Heap extension accounting

`mm_heap/mm_extend.c` in 12.13.0 enlarges the allocated tail guard into a
temporary block and frees it, without first crediting that temporary allocation
to `mm_curused`. The extension regression observed a wrapped peak-usage value
larger than the entire heap. `trmm_extend` holds the heap lock and credits the
extension size before invoking the unchanged upstream operation; its free then
restores the original current usage. The regression extends a half-size arena,
allocates from it, and checks that peak use remains bounded by arena size.

## Lock failure and cancellation

NuttX 12.13.0 `mm_lock` discards the return value of `nxmutex_lock` and returns
success. The temporary mutex therefore retries EINTR and panics on an unexpected
blocking acquisition failure; it must never let the allocator mutate an
unlocked heap. Trylock still returns a negative error when unavailable.
Upstream heap initialization/destruction also ignores mutex return values.
The provider requires successful semaphore initialization/destruction before
returning, so a failed teardown cannot be counted as complete or release the
heap's backing RAM while its synchronization object remains live.

Restoring cancellation immediately after `sem_wait` would allow pending
cancellation to kill the owner before the allocator releases its mutex.
The provider keeps the previous cancellation state in the mutex until the last
recursive unlock, posts the semaphore first, and only then restores that state.
The outer Tizen diagnostic lock must follow the same lifetime rule.

## Fault-time corruption checks

The normal `trmm_check` takes the backend mutex before running the bounded,
read-only node scanner. `trmm_check_quiescent` uses the same scanner without
acquiring locks or draining deferred frees. Its UP caller must disable interrupts
and verify that the Tizen metadata lock is idle. The bridge returns `-EAGAIN`
if the backend mutex is held, including by the faulting task: allocator
invariants cannot be trusted in the middle of a mutation. The caller must
distinguish unavailable diagnostics from confirmed corruption (`-EFAULT`).
The host regression checks idle and held mutexes, deliberate corruption, and
that the quiescent operation does not change mutex ownership.

## Current boundaries

SMP, NuttX mempool/TLSF/KASAN, NuttX backtrace storage, custom node guards,
nonzero free-delay tuning, and NuttX scheduler heap instrumentation are rejected
by the private profile. Tizen diagnostic metadata is a separate product layer.
NuttX dump/panic-on-failure options are also rejected; native heap identities
are not registered for those NuttX policies.
NuttX's `mm_memdump.c` is imported but not linked because Tizen diagnostic output
uses the bridge; NuttX `execinfo` and task-name APIs are not yet ported.
Protected userspace has no IRQ producer for the upstream delayed-free list;
that list's operations are compiled out there, matching upstream conditions.
The Tizen wrapper's pending-free list is shared with the kernel, which can
publish an app-heap free from IRQ/exit context. Protected kernel and user builds
therefore use the same lock-free pointer publication/detachment operations in
`pending_free.h`; plain user load/clear could discard a kernel enqueue between
those operations. Unsupported pointer-atomic targets fail a static assertion
rather than depending on an interrupt-unsafe `libatomic` lock. Flat builds
retain IRQ critical sections. The host regression exercises the exact shared
header with concurrent publishers and a single draining consumer; native IRQ
and protected syscall behavior still require target verification.

Run the independent host core test with:

```sh
os/mm/compat/tests/run.sh
```

It compiles the actual imported heap source with ASan and UBSan, checks allocator
operations and coalescence, deterministic overlapping realloc, four concurrent
host threads, multiple regions, extension accounting, deliberate guard corruption,
and simulated IRQ/task-exit delayed free. Its
host pthread adapter does not validate the native semaphore provider, firmware
privilege boundaries, or board/SMP behavior. QEMU and target evidence must be
recorded separately.
