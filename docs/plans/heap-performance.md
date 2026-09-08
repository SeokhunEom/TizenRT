# Heap performance improvement plan

Base: 29d2ed503. Scope: apps/examples/performance/heap only, plus this plan and focused tests. One shared application heap. No fragmentation or data-integrity workload.

1. Make heaptest_main own the run and wait for worker completion. Check thread creation, attributes, clock, allocation, and synchronization setup. Return nonzero on errors and emit one final PASS/FAIL. Join all created workers before releasing their state. Disable cancellation during the run to keep cleanup ownership explicit.
2. Preserve `heaptest INTERVAL REPEAT`; add strict named options, bounded numeric values, help, and correct units. Invalid input must fail without running. Support zero interval, configurable blocks, samples, warmup rounds, and a power-of-two size range.
3. Require monotonic clock configuration. Use 64-bit nanoseconds internally; report clock resolution. Time allocation and freeing in batches, not per call; preallocate bookkeeping outside timing. Report worker phase totals (min/median/max), amortized ns/call, run duration, launch delay, completion latency, and aggregate operations/s. Label timing overhead and quantization; do not claim individual-call worst-case latency.
4. Modes: single; samecpu and smp with equal priorities; samecpu-prio and smp-prio with an ascending priority ladder. Pin samecpu modes to CPU0 and spread SMP workers round-robin over configured CPUs. Reject SMP modes on UP. Select explicit FIFO/RR, reject RR if disabled. Same-CPU FIFO workers can serialize; mixed priorities do not guarantee lock overlap or prove priority inversion.
5. Use persistent pthread workers and per-worker semaphore launch gates. Workers acknowledge startup readiness; before every round the coordinator checks that each gate has a blocked waiter. The coordinator uses sched_lock only for the short gate-release setup (outside worker allocator timing), records a common epoch, releases all gates, and unlocks. Never hold a benchmark lock across malloc/free. Common-epoch latency includes dispatch/unlock overhead. No printf or control-memory allocation in measured loops. Collect all completions even after one allocation fails.
6. Verify strict parsing, counts/statistics, warmup exclusion, allocation/clock/thread-create failures and cleanup using a host harness with fault injection. Compile against real TizenRT headers in UP and SMP configurations if toolchain available. Run native TizenRT/QEMU scenarios if available; clearly distinguish host tests, target compilation, and physical SMP performance evidence.

Acceptance: exact operation counts for successful samples, no successful result on failed runs, no detached worker escaping command lifetime, matching compared workloads across modes, source-documented limits.

## Implementation status

- Implemented strict CLI, synchronous TASH entry, joined pthread workers and cleanup. TASH itself discards callback return codes; final PASS/FAIL is the shell automation contract.
- Implemented batch phase statistics, configurable workload/warmup, five placement/priority modes and explicit FIFO/RR.
- Added 67 host functional/fault scenarios and real-header ARM object compilation for flat SMP, flat UP/FIFO, protected SMP. All passed after fixing protected-build errno parsing and uint64 constant compatibility. The final 67-scenario run also passed with AddressSanitizer and UndefinedBehaviorSanitizer enabled for the host binaries.
- Docker daemon was unavailable; no firmware link/boot or actual SMP latency claim. Physical priority/affinity/PI behavior remains target validation.
