# Registry host tests

Run from the repository root:

```sh
make -C os/kernel/health_monitor/tests test
```

The default enables ASan/UBSan and writes executables under `/tmp/tizenrt-health-monitor-tests`. `HOSTCC`, `OUT_DIR`, and `SANITIZERS` can be overridden. Use a separate output directory when changing compiler or sanitizer flags.

Tests compile the production registry directly. Host shims supply a minimal TCB, current-thread lookup, system time, local IRQ state, and an atomic spinlock. They are not part of the kernel build. Operations use the kernel module's real interface; internal heap assertions add invariant checks without a product test interface.

Coverage includes initial state, duplicate/invalid registration, fixed capacity, late KICK without heap changes, root/middle/last removal, equal keys, wraparound including tick zero, overdue versus maximum-future ordering, explicit-target cleanup and PID/TCB reuse, publication overlap/sequence wrap, 50,000 model-checked operations, two concurrent registry workers, and concurrent snapshot publication/readers. Both SMP and UP models run.

These tests do not execute the target scheduler, real IRQ masking, ARM cache ordering, or the real task exit hooks. Compile the target kernel to check hook integration and separately test SMP task deletion/restart on the board. Timer/PANIC, ioctl, PM, and hardware watchdog are outside this stage.
