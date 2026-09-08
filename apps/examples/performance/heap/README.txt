Heap performance benchmark
==========================

Purpose
-------
Measure malloc/free performance on one shared application heap, including
same-CPU scheduling and SMP competition. This is not a fragmentation or data
integrity test. Workers allocate a batch of equal-sized blocks, then free that
batch in allocation order. No payload reads/writes are added.

Configuration
-------------
Enable CONFIG_EXAMPLES_HEAP_PERFORMANCE_TEST. Pthreads must be enabled; this
option selects CONFIG_CLOCK_MONOTONIC and CONFIG_LIBC_LONG_LONG for timing and
64-bit result formatting. SMP modes need CONFIG_SMP and at least two CPUs.
RR policy needs CONFIG_RR_INTERVAL > 0. CPU affinity is explicit on SMP builds.

The built-in TASH command is synchronous: the prompt returns only after the
benchmark joins its workers and prints its final result. The command entry
returns EXIT_SUCCESS/EXIT_FAILURE. The existing TASH dispatcher ignores callback
return values, so shell automation must consume the final
  Heap performance PASS (error=0)
or
  Heap performance FAIL (error=N)
line. This benchmark does not change TASH's global exit-status behavior.
Cancellation is disabled while the coordinator/workers own run state; the
coordinator restores its prior state after cleanup. Do not force-delete workers.

Usage
-----
  heaptest --help
  heaptest [INTERVAL_SECONDS REPEAT]
  heaptest [--mode MODE] [--workers N] [options]

The two positional arguments remain supported. For example, `heaptest 0 100`
uses no interval and 100 batches per sample. It now runs the default five
measured samples and one warmup per size, so total work exceeds the old test.
Named and positional arguments cannot be mixed. Invalid, missing, trailing,
negative or out-of-range numeric input fails without starting workers.

Options:
  --mode          single (default), samecpu, smp, samecpu-prio, smp-prio
  --workers       1..8; default 1 for single, 2 for other modes
  --blocks        1..256; default 100 simultaneous allocations per worker
  --repeat        1..100000; default 100 batches per worker per sample
  --samples       1..31; default 5 measured samples per size
  --warmup        0..10; default 1 unreported warmup round per size
  --interval      0..3600 seconds; default 1, BETWEEN sizes only
  --min-size      power of two, 16..1048576 bytes; default 16
  --max-size      power of two, >= min-size, <=1048576; default 8192
  --priority      within sched_get_priority_min/max(SCHED_FIFO); default 100
  --priority-step positive increment in priority modes; default 10
  --policy        fifo or rr; default rr if configured, otherwise fifo

Each size doubles until max-size. Mixed-priority worker i has priority
base + i*step; the entire ladder must fit the supported priority range.
A higher numeric priority means higher scheduling priority in TizenRT.

Modes and comparisons
---------------------
  single        One worker on CPU0: baseline without induced competition.
  samecpu       >=2 equal-priority workers, all on CPU0.
  smp           >=2 equal-priority workers, round-robin over configured CPUs.
  samecpu-prio  >=2 workers with ascending priorities, all on CPU0.
  smp-prio      >=2 workers with ascending priorities, spread across CPUs.

A UP build supports single/samecpu/samecpu-prio and rejects both SMP modes.
On an SMP build, samecpu modes deliberately leave other CPUs out of the workload.
With more workers than CPUs, smp modes also include per-CPU scheduling competition.
All configured CPUs used by a mode must be online; hotplug changes are not part
of this benchmark.

Examples (the same payload pattern and counts across comparisons):
  heaptest --mode single --blocks 32 --repeat 1000 --samples 5 --interval 0
  heaptest --mode samecpu --workers 2 --blocks 32 --repeat 1000 --samples 5 --interval 0 --policy rr
  heaptest --mode smp --workers 2 --blocks 32 --repeat 1000 --samples 5 --interval 0 --policy rr
  heaptest --mode samecpu-prio --workers 4 --priority 80 --priority-step 20 --blocks 32 --repeat 1000 --interval 0
  heaptest --mode smp-prio --workers 4 --priority 80 --priority-step 20 --blocks 32 --repeat 1000 --interval 0

There is NO synthetic yield/sleep in worker allocation loops. Equal-priority
FIFO workers on one CPU may execute serially. RR time slicing applies only
between equal-priority runnable workers. A higher-priority worker may finish
before a lower-priority worker begins. These are scheduler effects to observe,
not guarantees of overlapping lock acquisition or a priority-inversion proof.
The test measures combined allocator/scheduler effects, not isolated lock time.

Timing and results
------------------
Before each round all workers are confirmed blocked at their private launch
gates. The coordinator briefly uses sched_lock to record a common monotonic
epoch and post all gates, then unlocks. This launch operation is outside worker
allocator timing. No benchmark-owned lock is held around malloc/free. Launch
and completion latencies and aggregate makespan include gate-release, scheduler
unlock and dispatch overhead. Setup, thread creation, readiness polling, joins,
printing, warmup rounds, and between-size sleeps are not worker timings.

For each batch, timestamps bracket the allocation and free loops separately.
Per-sample phase totals sum these batch durations. Worker duration spans the
first allocation timestamp through the final free timestamp. Bookkeeping is
preallocated; workers do not log in the measured path. Timing still includes
loop/bookkeeping and clock overhead, interrupts, preemption and allocator locks.

For every size/worker the report prints:
  - exact malloc/free calls per measured sample (blocks * repeat each)
  - min/median/max of sample malloc totals and free totals, in nanoseconds
  - median phase total / calls as AMORTIZED ns/call
  - min/median/max worker duration, launch delay and completion latency
  - warning count for zero-duration allocation/free batch phases
Across workers it prints aggregate makespan (epoch to latest worker end), and
2 * workers * blocks * repeat / median_makespan as allocator calls per second.
Even sample counts use the midpoint of the two central values for the median.
No result is an individual-call maximum or hard real-time bound.

Nanosecond units do NOT imply nanosecond resolution. The actual monotonic clock
resolution is printed. If phases are below it, the separated phase estimates
can be noisy or zero; use a suitable high-resolution target clock and/or larger
batches. Increasing repeats makes whole runs longer but does not remove the
quantization of each short phase. No throughput is computed for zero makespan.

Keep board/clock speed, background workload, allocator configuration and debug
instrumentation fixed for before/after comparisons. The report prints board
(when configured), CPU count, policy, PI configuration and selected heap debug
features. Retain the full .config and firmware revision with captured output.
PI 'configured' is not proof of the protocol on every heap lock. One shared app
heap is assumed; no heap-selection or cross-heap comparison is implemented.

Memory and failures
-------------------
Maximum simultaneous PAYLOAD is workers * blocks * max-size. Heap metadata,
run/results bookkeeping, pointer arrays and worker stacks (6144 bytes each)
are additional. Reduce blocks/size/workers on small heaps. More workers also
change the bookkeeping footprint. The benchmark does not silently shrink the
workload or skip an allocation failure, since that would invalidate comparisons.

Any failed warmup or measured round fails the run. A failed allocation frees
its successful prefix; all other workers finish their round before shutdown.
Partial worker creation joins already-created workers. Clock and attribute
errors also produce FAIL. Completed earlier size reports may remain in the log;
the final FAIL makes the overall run unsuccessful. A join error preserves run
storage rather than freeing memory a worker might still reference. Recovery
from corrupted semaphore objects, externally deleted workers, or CPUs taken
offline during a run is outside the supported lifecycle.

Validation
----------
  python3 apps/examples/performance/heap/tests/run_host_tests.py
  HEAP_HOST_CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" python3 apps/examples/performance/heap/tests/run_host_tests.py

Host tests execute the production source with simulated TizenRT semaphore,
scheduling and affinity adapters and allocation/clock/creation fault injection.
They verify counts, statistics, strict input rejection, worker joins and cleanup.
Their timings are NOT target performance evidence. The script also compiles ARM
objects against real TizenRT headers using temporary config overlays for flat
SMP, flat UP/FIFO, and protected SMP. Set CC to a Clang supporting arm-none-eabi.
These object checks are not a firmware link/boot or physical SMP validation.
