# Health Monitor core

Status: initial opt-in implementation, September 2026.

Reference: [original design, 20312cdb](https://github.com/seokhun-eom24/TizenRT/commit/20312cdb1d098e9c04645eb681c93ea72d2b0fc3).
This document defines the implemented subset and supersedes that proposal for
this patch. The old task_monitor, prctl ABI, scheduling hooks and configurations
are preserved. No existing task is automatically registered.

## Scope and trade-off

The first core supports **one CPU, periodic ticks, no tick suppression and no
loadable binary separation**. Kconfig and compile-time guards reject unsupported
configurations. In particular, the original rtl8730e SMP/loadable configuration
cannot enable this implementation unchanged.

Local IRQ masking serializes source/heap updates against the tick and task
release. No semaphore, mutex, spinlock, scheduler lock, allocation, VFS operation,
sleep, logging or retry loop occurs in start/stop/kick. Masking is restored to its
previous state, including when the caller already has IRQs disabled. This is not
an SMP publication protocol; enabling SMP by removing the guard is incorrect.

A UP indexed heap is a deliberately smaller design than a CPU0-only derived heap
with cross-CPU atomic slots and membership epochs. It avoids a full registration
scan even on start and stop. The remaining cost is bounded IRQ latency, not lock
waiting. Absolute latency still needs target measurement.

## Public contract

Enable CONFIG_HEALTH_MONITOR, include <tinyara/health_monitor.h>, and call:

```c
int ret = health_monitor_start(5000);
if (ret < 0) {
    /* Module-owned error handling: monitoring was not established. */
    return ret;
}
/* Do work; do not register again inside a nested callback. */
ret = health_monitor_kick();  /* Only after meaningful progress. */
if (ret < 0) {
    return ret;
}
return health_monitor_stop(); /* Before normal exit or an unmonitored wait. */
```

These are kernel C APIs, not a new protected-user syscall/device ABI. Each call
operates on sched_self(), returns zero or **negative errno**, and does not write
libc errno. ISR calls return -EPERM; idle/zero timeout return -EINVAL; duplicate
start returns -EALREADY; inactive kick/stop return -ENOENT. Once a fault is latched,
operations return -ESHUTDOWN. They are not async-signal-safe. Integrations must
check results; there are no mandatory integrations in this patch.

Timeout conversion is ceiling(timeout_ms * 1000 / USEC_PER_TICK), using 64-bit
arithmetic. Timing has tick resolution, so wall-clock duration can differ by up
to one tick depending on phase. All nonzero uint32_t millisecond values work.
Health Time is a separate 64-bit periodic-interrupt counter; PM missing-tick
compensation never advances it. Tickless and tick suppression are excluded until
awake-time accounting is integrated. As with other software tick clocks, lost
interrupts are not elapsed-time evidence. 64-bit rollover (billions of years at
typical tick rates) and memory-corruption self-checks are not implemented.

Expiration is now >= deadline. The tick runs before legacy watchdog feeding and
before timer callbacks. A kick/stop completed before the deadline tick wins;
once time reaches the deadline, neither can erase failure. A late API detects
this itself, latches timeout and returns -ETIMEDOUT without running diagnostics
in the caller. Diagnostics execute on the next tick.

## Index and cost

Fixed source slots use PIDHASH, with full PID identity checks and no TCB pointers.
There is one node per active registration, with the slot recording its heap
position. Stop removes the node before another registration can reuse the slot,
so stale generations do not survive a restart. Task release latches the first
exit fault before PID/TCB release; all further starts are rejected for that boot.

| Operation | Work under local IRQ exclusion |
| --- | --- |
| start | Direct source lookup and one O(log N) heap insertion |
| kick | O(1) source deadline update; no heap mutation |
| stop | O(log N) indexed removal; no registration scan |
| healthy tick with future root | O(1), inspect only cached earliest deadline |
| due cached root | Read that source; fail if expired, otherwise sift down |
| fatal phase | Walk all registrations and live tasks, then panic |

A cached deadline is always a lower bound on its source deadline. Therefore a
future root proves every registration is still within its deadline. Renewing a
root exposes the next candidate, including an expired task hidden behind a
renewed one. On UP no writer runs during the ISR: every stale node can be repaired
at most once that tick. Worst-case synchronized renewals cost O(N log N), bounded
by CONFIG_MAX_TASKS; there is no early budget exit that could hide a timeout.
There is no periodic full scan or full heap rebuild.

## Failure and diagnostics

Timeout or release of an armed task latches the first reason, PID and deadline.
Exits, cancellation and deletion that reach sched_releasetcb are covered. A task
outside a Scoped Monitoring interval has no contract. Managed Binary Teardown is
not wired yet, so APP_BINARY_SEPARATION configurations are excluded.

The periodic ISR outputs a bounded, single-line header via architecture up_putc,
using a fixed-width hexadecimal formatter without printf or buffered logging.
It contains raw reason (1 timeout, 2 exit), PID, Health Time and deadline. If
CONFIG_SYSTEM_REBOOT_REASON is enabled, the existing REBOOT_SYSTEM_WATCHDOG reason
is written immediately afterward. Board ports must verify up_putc is polling
low-level output; UART failure can prevent even the header from completing.

Next it dumps all active registrations, all live tasks' PID/state/priority,
scheduler lock count, wait semaphore and stack address, and calls PANIC for
architecture-specific registers/stacks and normal crash handling. A final endless
loop prevents recovery if PANIC unexpectedly returns. A released task's scalar
registration survives, but its TCB/stack snapshot is not retained. Semaphore
holder chains and exhaustive lock ownership are not implemented.

**This patch does not initialize or own a hardware watchdog.** If interrupts stop,
software monitoring cannot diagnose the fault. If UART, task traversal or panic
blocks, reset is not guaranteed. Existing watchdog feeding/panic-disable behavior
is unchanged. Guaranteed watchdog reset during stalled diagnostics requires a
follow-up board adapter and panic/PM integration. Do not treat this core as the
original design's production fail-safe watchdog implementation.

## Validation

Run `python3 tests/health_monitor/run.py`. It compiles the production C source
with fake UP IRQ/scheduler/UART/panic adapters and AddressSanitizer plus UBSan.
Coverage includes API misuse, ceiling conversion, UINT32_MAX timeout, 32-bit tick
carry, exact expiration, stale-root repair, simultaneous renewals, unexpected
release, PID reuse, first fault preservation, terminal APIs, fatal output order,
and 100,000 seeded membership operations with independent heap invariants.

ARM object compilation with the repository's real headers is also provided by
`python3 tests/health_monitor/compile_arm.py` (requires clang with ARM support).
It uses qemu/tc_16m, disables the legacy HEAPINFO_USER_GROUP setting whose generated
header is absent, and builds the core plus both scheduler hook translation units.
This is compilation evidence, not a linked firmware or QEMU runtime test.

Full firmware build, physical timing, PM, UART/persistent reason, and hardware
fault/reset injection are not validated in this environment. Docker is installed
but its daemon and launchable Desktop executable are unavailable.
