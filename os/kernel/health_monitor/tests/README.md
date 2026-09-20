# Health monitor host tests

For real ARM UP execution, controlled deadline boundaries and the preserved
QEMU validation results, see [QEMU tests](qemu/README.md).

Run from the repository root:

```sh
make -C os/kernel/health_monitor/tests test
```

The default enables ASan/UBSan and writes executables under `/tmp/tizenrt-health-monitor-tests`. `HOSTCC`, `OUT_DIR`, and `SANITIZERS` can be overridden. Use a separate output directory when changing compiler or sanitizer flags. Under a ptrace-based sandbox, LeakSanitizer cannot run; use `ASAN_OPTIONS=detect_leaks=0 make -C os/kernel/health_monitor/tests test` there. This disables leak checking only, not ASan/UBSan's other checks.

## Registry tests

Tests compile the production registry directly. Host shims supply a minimal TCB, current-thread lookup, system time, local IRQ state, and an atomic spinlock. They are not part of the kernel build. Operations use the kernel module's real interface; internal heap assertions add invariant checks without a product test interface.

Coverage includes initial state, duplicate/invalid registration, fixed capacity, late KICK without heap changes, root/middle/last removal, equal keys, wraparound including tick zero, overdue versus maximum-future ordering, explicit-target cleanup and PID/TCB reuse, publication overlap/sequence wrap, 50,000 model-checked operations, two concurrent registry workers, and concurrent snapshot publication/readers. Both SMP and UP models run.

These tests do not execute the target scheduler, real IRQ masking, ARM cache ordering, or the real task exit hooks. Compile the target kernel to check hook integration and separately test SMP task deletion/restart on the board.

## Driver/VFS tests

`driver_test.c` compiles the production registry, ioctl driver, VFS `open`/`close`/`file_close`, `fs_getfilep`, and `fs_ioctl`/`file_ioctl`. Host shims supply static inode/fd storage and task/IRQ primitives. Target `fcntl.h` and `sys/ioctl.h` are used to avoid Linux access-flag and ioctl-encoding differences. The host `open`/`close` symbols are not replaced. Only the pre-existing VFS `vopen` unused-parameter warning is suppressed locally for the no-mountpoint test configuration.

The driver tests verify registration-error propagation, open without START, unsupported read/write, invalid commands/arguments/fds, VFS errno conversion, late KICK, shared-fd caller identity, independent STOP, close without unregistering either thread, reopening, and explicit-target cleanup. The SMP model also runs two threads through 10,000 START/KICK/STOP cycles each on one shared fd.

Inode registration/allocation, fd allocation/release, and cancellation behavior remain host models. The target's variadic libc ioctl wrapper and protected-build SVC transition are not executed here; tests enter at `fs_ioctl`. The tests do not simulate closing/reusing a descriptor concurrently with its use: applications must keep the fd valid for ongoing calls.

## Board smoke example

Add [board_smoke.c](board_smoke.c) to a test application's sources and call `health_monitor_smoke()` from a thread while `CONFIG_HEALTH_MONITOR=y`. It returns zero or negative errno after open → START → KICK → STOP → close, using only public headers and unsigned-long ioctl arguments. It is not included in the product or host-test build automatically.

The smoke example exercises registration operations only. A successful smoke run does not verify deadline enforcement or reset. The system tick now enforces deadlines, but PM wakeup and hardware watchdog integration are still pending. Keep the target awake when testing timeout behavior; the example itself stops before its deadline.

## Timer tests

`timer_test.c` compiles the production registry, `sched_process_timer()` and the common assert reboot-reason helper. Host shims supply the clock increment, CPU identity, local IRQ/global-lock state and reboot-reason storage. PANIC is intercepted with `setjmp`/`longjmp`: it verifies the lock/reason ordering but does not execute ARM diagnostics or reset the host.

UP, SMP and reboot-reason-disabled variants run with the same ASan/UBSan flags as the registry/driver tests. Together these are seven executables. The SMP registry/driver tests use POSIX pthread barriers; run the full suite on a Linux host or in a Linux container. The new timer tests do not need those barriers.

Coverage includes:

- Empty/future/publishing hints, CPU0-only inspection, exact deadline equality and a late KICK accepted before inspection.
- Sustained normal KICK, STOP/cleanup before inspection, wraparound through tick zero and overdue versus far-future reservations.
- All 256 candidates, both equal and different reservation times, including a single expired target behind renewed roots.
- Exactly one lock attempt on contention or injected weak-CAS failure, followed by inspection on a later tick.
- A cached due hint changed by STOP/KICK before acquisition, and KICK/cleanup/free after the final verdict but before PANIC.
- One fixed `now` per pass, updated system time before inspection, inspection before CPU-load/global-lock/watchdog work, and repeated tick calls as used by board catch-up processing.
- New reason 62 written outside the registry lock and retained by the actual common assert-reason helper; PANIC also works when reason recording is disabled.

Only the CAS primitive is interposed to inject failure and deterministic interleavings; the production weak/acquire/relaxed parameters are checked and successful attempts use the host's real CAS. The native ARM instruction sequence must be inspected separately to verify that the target compiler emits no retry loop or atomic helper call. These tests do not prove ARM cache ordering, ISR latency, real scheduler teardown races, board reboot-reason persistence or hardware reset.
