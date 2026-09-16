# Health monitor host tests

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

Stage 3 connects registration operations only. Timer/PANIC, PM wakeup, and hardware watchdog integration are still outside this stage. A successful smoke run does not verify deadline enforcement or reset.
