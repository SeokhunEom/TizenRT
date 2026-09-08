# Health Monitor core

Status: opt-in UP and amebasmart SMP core, September 2026. Host/model and
cross-compilation validation completed; physical SMP validation remains open.

Reference: [original proposal, 20312cdb](https://github.com/seokhun-eom24/TizenRT/commit/20312cdb1d098e9c04645eb681c93ea72d2b0fc3).
This specification supersedes the earlier UP-only subset for this branch.
The existing task_monitor, prctl ABI and registrations are preserved. There are
no automatic or mandatory task integrations and no new user-space syscall ABI.

## Supported configurations

- UP with real periodic timer interrupts.
- SMP on ARCH_CHIP_AMEBASMART (RTL8730E), with CPU0 as the monitor executor.
- Tickless, tick suppression, fake/suppressed timer interrupts and
  APP_BINARY_SEPARATION are rejected by Kconfig and C guards.
- Other SMP ports require explicit architecture/publication validation before
  adding them to the support gate.

The original rtl8730e/loadable_ext_ddr_st7785 defconfig is still not an enable-as-is
configuration: binary separation and tick suppression must remain off for this
core. Managed Binary Teardown and PM sleep-time accounting are separate work.
No defconfig was enabled or changed by this patch.

The port requires aligned coherent 32-bit shared RAM accesses. The checked source
maps SMP RAM with PMD_SECT_S (armv7-a/mmu.h) and enables SMP coherency on CPU0 and
CPU1 in amebasmart_boot.c / amebasmart_cpuboot.c. Its timer initialization enables
the timer on CPU0. These are source checks, not proof of the physical board's
final page tables or timing.

## Ownership and memory access

No target code uses C11/GCC atomic APIs, libatomic, LDREX/STREX, CAS, fetch-add,
shared read-modify-write counters, or spinlock acquisition. The platform access
header emits individual LDR/STR word accesses and a full DMB SY with compiler
memory clobbers. Volatile alone is not the synchronization protocol. This is a
kernel/architecture contract, not portable ISO C thread synchronization.

| Data | Writer | Reader |
| --- | --- | --- |
| Source publication | The task associated with that PID slot | CPU0, owner, release path |
| Per-CPU queue head and entries | Producers on that CPU, under local IRQ masking | CPU0 |
| Per-CPU queue tail | CPU0 | That CPU's producers |
| Per-CPU progress / fault mailbox | Producers on that CPU, under local IRQ masking | CPU0 |
| Time publication and shutdown | CPU0 | All CPUs |
| Heap, cached snapshots, first confirmed fault | CPU0 | CPU0 |

Task execution must already be serialized across migration by the scheduler.
PID reuse must occur after sched_releasetcb's release hook. The release hook
never writes the task source slot. No new scheduler lock is introduced to obtain
these properties. APIs are self-only, task-context-only and not signal-safe;
local IRQ masking prevents migration/reentrant producers inside an operation.
Existing scheduler critical sections in task deletion are not new monitor locks.

## API and error contract

Include <tinyara/health_monitor.h> and check every return value:

| API | Success |
| --- | --- |
| health_monitor_start(timeout_ms) | Publishes one active self contract and a membership hint |
| health_monitor_kick() | Publishes an explicit Liveness Checkpoint; no queue/heap operation |
| health_monitor_stop() | Publishes inactive state and a membership hint |

Zero means success; errors are negative errno values, without changing libc
errno. ISR use returns -EPERM, idle/zero timeout -EINVAL, duplicate start
-EALREADY and inactive/wrong-PID kick or stop -ENOENT. A confirmed global fault,
or a fault already reported on the calling CPU, returns -ESHUTDOWN.

-EAGAIN means the clock changed across its high-word carry while being read.
-ENOSPC means the local membership queue is full. Neither changes the contract:
a failed start creates no registration; a failed stop remains armed. Check these
results and retry at a later normal execution opportunity if appropriate. Do
not spin until success or exit after an unsuccessful stop. Kick works even when
the membership queue is full.

-EIO reports abandoned/inconsistent self publication; -EOVERFLOW reports exhausted
publication identity or time arithmetic. These errors publish a sticky fault on
the calling CPU. -ETIMEDOUT from kick/stop also reports a sticky fault and leaves
the source unchanged. CPU0 performs diagnostics at the next observation.

## Publication and identity

Source slots are fixed, indexed by PIDHASH, and store the full PID. Shared payload
fields use 32-bit accesses, including both halves of a 64-bit deadline/timeout.
Each publication changes an even sequence to odd, applies the payload, then
publishes the next even sequence, with full ordering barriers. Kick changes only
sequence identity and deadline words; start/stop also update metadata.

An epoch word advances on low sequence wrap, while the sequence is odd. A reader
brackets the low sequence and payload with epoch reads and barriers, accepting
only equal epoch/sequence and an even sequence. Each read is one attempt: no
retry-until-stable loop. Source and per-CPU progress identities support 2^63
completed publications before fail-closed exhaustion, so high-rate kicks do not
exhaust a 32-bit sequence after weeks. No shared 64-bit atomic access is required.

A stopped slot may be reused by another PID after scheduler release. Old queue
entries hold slot indices, not TCB pointers or commands to replay. A slot left
active by unexpected release cannot be overwritten by a new start, and its exit
fault persists in a separate per-CPU mailbox.

## Membership and CPU migration

Each CPU has CONFIG_MAX_TASKS usable SPSC queue entries, statically allocated.
A start/stop checks capacity before changing its source, then publishes its slot
index and head. CPU0 snapshots each head once per tick, consumes at most that
snapshot, and publishes tail only after reading an entry. A producer cannot
extend the current tick's work indefinitely.

Hints mean “read this slot's current state,” not “apply this historical start or
stop.” This permits coalescing and out-of-order arrival across CPU migration:
start on CPU1, stop/restart on CPU0, then CPU1's old hint all converge on the same
latest source. A successful stop checked its own deadline first; late kick/stop
and unexpected exit are preserved independently in sticky fault mailboxes, so
coalescing cannot erase those reported failures.

An unreadable source does not remain at the queue head. CPU0 records a heap retry
for that slot and continues with other hints, so a busy writer cannot hide an
unrelated new registration. There is at most one heap entry per source slot.

## Time, timeout and overlap semantics

Health Time is a private CPU0 64-bit periodic-tick count with a read-only shared
word publication. Other CPUs do not increment it. A high-word carry is protected
by a sequence; ordinary ticks publish only the low word. Every nonzero uint32_t
millisecond timeout is converted with ceiling(timeout_ms * 1000 / USEC_PER_TICK).
Resolution is one tick, including phase quantization.

Only delivered periodic ticks count. PM sleep compensation, lost-interrupt
reconstruction and actual elapsed wall time are not independently measured by
this core. The supported non-tick-suppressed board path must deliver awake ticks;
PM/tickless ports need an explicit time adapter.

Start uses the time sampled inside its call; queue handling does not restart its
timeout. Kick/stop check that sampled time against the current source deadline
before publishing. A late call reports timeout instead of changing the source.
An operation overlapping a tick can finish after the tick's time publication;
its time check and CPU0's source observation define the boundary, not an
unimplemented global CAS order.

CPU0 confirms timeout when it accepts an active source snapshot whose deadline
is <= its current time. That accepted snapshot is the decision point. A later
concurrent stop/kick does not retract it. A call already in progress can finish
successfully while CPU0 confirms shutdown; success is not a guarantee against a
concurrent fatal observation. New membership is normally seen on the next tick;
a publication overlapping the consumer snapshot can require the following tick.

## Lazy heap and bounded publication recovery

The heap and its indexed positions are CPU0-private. Start/stop do not sort it.
CPU0 inserts/removes/reorders only slots identified by membership hints. Kick
leaves the heap's lower-bound deadline untouched. A future root, after processing
the captured membership hints, needs no source read. A due root is refreshed from
its source; a renewed root is sifted down to expose the next candidate.

An unreadable root gets a retry key of now + 1. Other due roots are checked in the
same tick. If the same unreadable epoch/sequence remains for two additional ticks,
CPU0 confirms a publication failure. Changing versions reset this stall timer:
the writer completed a publication between versions, and kick/stop validate
lateness before writing. Healthy rapid publication is not classified as stalled
merely because two reads collide. Once a writer stops progressing, it cannot
obtain unbounded grace by leaving the sequence odd.

Per-CPU progress publication additionally detects a producer stopped before it
publishes its queue head or fault-ready word. An unchanged odd progress identity
for two additional observed ticks is a publication failure. The first observation
starts that interval. A CPU that stops outside a monitor API is detected only
through expiration of its registered tasks; there is no general CPU heartbeat.

| Operation | Normal work |
| --- | --- |
| start / stop | O(1) publication and bounded queue reservation, local IRQ masking |
| kick | O(1) clock/source reads and checkpoint publication, no queue or heap |
| Tick, no hints and future root | O(number of CPUs), zero source reads |
| Membership | O(K log N), K limited to captured queue entries |
| Due/uncertain roots | O(R log N), each candidate is removed or gets a future key |
| Fatal | Full registration and live-task walks, then architecture panic |

No allocation, semaphore, scheduler-global critical section, spinlock, VFS,
sleep, I/O, or waiting for another CPU occurs in normal API/monitor processing.
DMB and memory traffic have hardware latency: this does not claim a measured
wall-clock upper bound. Full queue bursts and synchronized renewals still have
bounded but nontrivial ISR cost. Target latency and static RAM/map measurements
remain required before production enablement.

## Fault diagnostics and limits

Per-CPU fault mailboxes are immutable after their ready word is published. CPU0
checks mailboxes in CPU order before checking deadlines, and alone confirms the
first fault. “First” means first CPU0-confirmed observation, not globally earliest
wall-clock occurrence among simultaneous CPUs.

The bounded raw UART header precedes scheduler traversal or CPU pause. Reasons
are 1 timeout, 2 unexpected exit, 3 publication failure, 4 identity/time exhaustion.
It records PID (or -1 when unavailable), source slot (or UINT32_MAX), reporting
CPU, Health Time and deadline. Existing
REBOOT_SYSTEM_WATCHDOG is persisted immediately afterward when configured.
Registration caches and all source slots (including unindexed registrations) are
then dumped, followed by raw per-CPU queue/progress/fault words and live task PID/state/priority,
scheduler lock count, wait semaphore and stack address, and PANIC for the board's
register/stack diagnostics. Unreadable new registrations can have no cached PID
or deadline; they are not fabricated from torn payload. Exited TCBs are not kept.

In SMP, sched_foreach and architecture panic can acquire existing global locks
or pause other CPUs. These operations occur only AFTER the raw header/reboot
reason. A stuck lock, inaccessible UART, failed CPU pause or panic can stall the
detailed dump. This patch does not provision/own an HW watchdog or change panic's
watchdog behavior, so reset during such a stall or CPU0 tick loss is not guaranteed.
No memory-corruption audit, semaphore-holder-chain walk, automatic kernel-thread
integration, device ABI, or Managed Binary Teardown integration is included.

## Reproducible validation

- `python3 tests/health_monitor/run.py`: production core with deterministic UP/SMP
  adapters, ASan/UBSan, 100,000 churn operations per configuration, and native
  two-thread stress (320,000 API calls concurrent with 5,000 CPU0 ticks).
- `HEALTH_TSAN=1 python3 tests/health_monitor/run.py`: additionally selects TSan for
  native-thread stress. No sanitizer errors observed.
- `python3 tests/health_monitor/compile_arm.py`: real TizenRT headers for QEMU
  Cortex-M3 UP and RTL8730E Cortex-A32 SMP, enabled/disabled scheduler hooks, and
  optimized assembly audit rejecting exclusive instructions, atomic helpers and
  waiting/allocation APIs. Unsupported SMP ports are rejected.

Tests cover queue-full rollback, stale-root exposure, migration/restart/PID reuse,
release, deadline edges, time and sequence carry, snapshot tearing, producer stall,
terminal faults, fatal output ordering and access-boundary interleavings. The
threaded host adapter uses host-only atomic word operations to model coherent
accesses for sanitizers. It is stronger than ARM's weak memory behavior and does
not prove the target barriers, MMU attributes or timing.

The object builds disable binary separation/tick suppression and the legacy
HEAPINFO_USER_GROUP generated-header dependency in temporary test configs only.
The SMP scheduler build also required its existing irq_cpu_locked declaration;
sched_processtimer.c now includes the owning irq/irq.h header.

Full linked firmware, actual SMP RTOS execution, physical CPU migration/cancel
stress, PM time behavior, UART/reboot reason and hardware fault/reset injection
have not been validated. The local Docker daemon is unavailable and there is no
arm-none-eabi-gcc on PATH. Clang object compilation is not firmware build evidence.

Implementation commit and dated Korean validation report:
[Health Monitor SMP 구현 및 검증 보고서](health_monitor_implementation_validation.md).

A flat-build TASH example is available at
[apps/examples/health_monitor](../apps/examples/health_monitor/README.md).
