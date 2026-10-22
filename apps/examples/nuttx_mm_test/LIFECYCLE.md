# Protected application heap lifecycle verification

Enable `CONFIG_EXAMPLES_NUTTX_MM_LIFECYCLE=y` in `qemu-armv8m/loadable_all`
or `loadable_apps` and rebuild the kernel, common library, app1 and app2.
The option requires two loadable apps and disables neither the MPU nor the
normal public allocator path. Fault recovery, retained-arena optimized reload
and XIP ELF are outside this test. The flat `nuttx_mm_test` remains separate.

App1 retains the existing TASH startup. App2 replaces the micom sample's endless
loop with a bounded workload: calloc zero-fill, aligned allocation, two joined
pthread workers performing 256 allocation/reallocation/data checks each, then
one detached child and a separate `task_create` task group. The main thread leaves a 257-byte allocation live and
returns normally. The detached child waits briefly, allocates and frees memory,
and reports completion. The kernel accepts that report only after the original
main PID has disappeared and the pthread shares the original group. The separate
task waits longer, allocates and frees memory, and reports only after both the
main and pthread PIDs disappear. The kernel also checks that its task group
differs from the original group. The app heap must survive both exit boundaries.

Run from the repository root, using a new output directory:

```bash
/opt/homebrew/bin/python3.11 .github/scripts/qemu-armv8m-mm-lifecycle.py \
  --config loadable_all \
  --output build/nuttx-mm-validation/loadable-all-lifecycle --timeout 240
```

Repeat after a clean `loadable_apps` build, changing `--config` and the output
directory. The runner stages fresh A/B packages using the existing QEMU staging
helper; it never reuses an old RAM image. The boot-time app2 run is a warm-up.
At TASH, `nuttx_mm_life` starts three further real Binary Manager loads.
The kernel prints `NXMM LIFE ROUND BEGIN round=N` immediately before each load;
the runner uses this boundary to exclude any remaining boot warm-up output.
Each round requires fresh main, final-child and separate-group success markers with matching
PIDs, followed by the cleanup marker.

The privileged test controller checks all of the following after each round:

- Before exit, both endpoints of the live main (257-byte), late pthread
  (521-byte) and separate group (733-byte) allocations resolve through
  the kernel's active heap registry to this round's app2 heap. This establishes
  address residency in the app heap, rather than relying on user-side success.
- All three reported app task PIDs are gone and Binary Manager reports inactive.
- The exact app heap is absent from the active and inactive registry, and the
  common library's heap-table slot is zero.
- No delayed kernel/user free or semaphore-list reference points inside the
  former app RAM allocation. With fault recovery disabled, an empty semaphore
  registry alone is not evidence that the two heap locks were destroyed.
- The test-gated native lifecycle snapshot records exactly one successful heap
  initialization and one uninitialization, with the destroyed heap matching
  this round. The uninitialization counter advances only after both mutex
  destructors succeed and the backend handle has been cleared.
- Every kernel heap's arena, used bytes and free bytes return to the baseline
  taken after the boot-time app2 teardown. A bounded wait permits deferred
  cleanup; a timeout or partial cleanup fails the test.

The final marker is `NXMM LIFE RESULT PASS cycles=3`; `ps` must then show that
the controller exited. The runner preserves serial output, firmware/package
and configuration hashes, Git state and per-round receipts in the output
directory. Run the existing full `network_tc`/`kernel_tc` runner separately on
the same firmware for broader regressions.

The Python tests in `.github/scripts/tests/test_qemu_armv8m_mm_lifecycle.py`
verify rejection of stale, duplicate, reordered, failed and incomplete serial
evidence. They are runner tests, not substitutes for protected target runs.

This covers normal app exit and normal reload, including a main thread that
finishes before its last pthread and a separate group that outlives both.
It does not validate forced fault recovery,
SMP, actual interrupt-context free ordering, or physical hardware.
