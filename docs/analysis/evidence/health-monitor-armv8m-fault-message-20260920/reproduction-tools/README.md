# ARMv8-M Health Monitor validation

Run with Python 3.10+ against the `codex/qemu-armv8m-kernel-tc` worktree.
The runner imports that port's QEMU command builders and A/B staging code.
It never loads a package at an independently invented address.

```sh
/opt/homebrew/bin/python3 tools/qemu-armv8m-health-monitor/run.py \
  --root /path/to/qemu-armv8m-kernel-tc --profile hello \
  --case all --rounds 3 --output /tmp/hm-flat-new-run
```

Each output directory must be new. Results include the ELF/config SHA-256,
source HEAD, QEMU command, serial log, and detailed per-round observations.
The source HEAD alone does not identify an uncommitted build; retain the
source patch/manifest and effective config with the build artifacts.

## Build variants

Use Homebrew Bash and the port's `os/dbuild.sh menu` clean/reconfigure/build
workflow. Select `qemu-armv8m` and one of `hello`, `loadable_all`,
`loadable_apps`, or `xip_all`. Preserve each effective config before changing
variants. Use the complete archived config, because this port's configure
step does not populate omitted Kconfig defaults.

- All variants: `CONFIG_HEALTH_MONITOR=y`, example enabled, board device registration.
- Flat probes: `CONFIG_EXAMPLES_HEALTH_MONITOR_QEMU=y`; TMPFS and its complete
  size/heap settings are required for the concurrent file-I/O case. Set
  `CONFIG_MAX_TASKS=256` to exercise 128 real monitored workers.
- Capacity: flat probes plus `CONFIG_QEMU_HEALTH_MONITOR_CAPACITY=4`.
- Protected user fixture: `CONFIG_EXAMPLES_HEALTH_MONITOR_ARMV8M=y`, flat probes
  disabled, `CONFIG_BINMGR_RECOVERY=y`, `CONFIG_BINMGR_RELOAD_REBOOT` disabled.
  Include `loadable_apps/health_monitor/health_monitor_user.inc` through the
  guarded wifiapp/micomapp entry points preserved in the source patch.
- Final production-style validation: both test fixtures disabled; original
  recipe task limits/filesystems and binary-reload policy restored. Health
  Monitor and its ordinary example remain enabled.

## Cases

- `basic`, `normal`: public example operations; normal includes repeated worker
  exits, kernel_tc, and post-suite TASH/Health Monitor checks.
- `serial`: console TCGETS/TCSETS followed by a required TASH response.
- `all`: API, shared fd, lifecycle, forced allocator-cursor PID reuse,
  controlled tick boundaries, concurrent file I/O, 70,000-cycle stress per
  round, and up to 128 workers. Synthetic time and cursor controls are
  confined to the flat test build.
- `fatal-close`, `fatal-equality`, `fatal-overdue`, `fatal-late`, `fatal-wrap`,
  `fatal-stale`, `fatal-multi`, `fatal-equal`, `fatal-far`, `fatal-unstable`:
  require exactly the intended Health Monitor PANIC and TIMER0 IRQ 19.
- `capacity`: full static registry, ENOSPC without mutation, and slot reuse.
- `reset --ms 1000` / `--ms 2000`: natural deadline, exact panic location,
  IRQ, host time tolerance and QEMU software reset. This is not physical
  watchdog/reset or reboot-reason persistence evidence.
- `user-api`, `user-return`, `user-expire`, `user-reload`: app1/app2 public
  syscall tests, unprivileged CONTROL verification, registered-main exit,
  kernel-side expiration and real Binary Manager reload. `user-reload`
  invokes the recovery entry point from a test kernel command; it does not
  claim to inject a hardware user-space fault.

Protected fixtures do not call private Health Monitor functions or read
TCBs. The control client enters the existing OS API test driver through ioctl.
Only its guarded kernel observer reads registrations. A delayed LPWORK
callback invokes the real recovery path after the requesting syscall returns.
app1 retains the original preapp/TASH startup, including after reload. See the validation report for actual completed
cases, failed attempts and limitations.

## Diagnostic comparisons

`user-reload-empty` reloads with no active registrations and records kernel
heap allocation owners. `user-return-empty` exits app1 without START, and
`user-return-app2` exits the standalone second app with registration. The
checks remain strict: known loader leaks or a fault after main exit still
produce a failing result. Consult the report before interpreting these as
Health Monitor regressions.

The normal original app1 starts TASH as a child. Preserve its preapp call in
the guarded fixture entry point. Rebuild cleanly after editing a user `.inc`
file; this port's incremental app dependency tracking can miss that change.

## CPU fault message delivery only

The current requested scope excludes Binary Manager recovery/reload and
unload delays. The earlier recovery cases above remain historical diagnostics.
Use `fault-message.py` to execute a real `udf #0` or read protected kernel RAM
from the app main thread, then observe the real fault sender and MQ receiver.
The app can have a 60-second Health Monitor registration or no registration.

Build a protected fixture as described above and archive `effective.config`
plus the complete `bin/` output in a new build directory. The fault sender is
compiled under `CONFIG_BINMGR_RECOVERY`, so that option must be enabled and
`CONFIG_BINMGR_RELOAD_REBOOT` disabled for this test. Recovery itself is stopped
before its call instruction; the runner never resumes or detaches at that point.

```sh
/opt/homebrew/bin/python3 tools/qemu-armv8m-health-monitor/fault-debug-metadata.py \
  /tmp/saved-fault-build
/opt/homebrew/bin/python3 tools/qemu-armv8m-health-monitor/fault-message.py \
  --root /path/to/qemu-armv8m-kernel-tc --build /tmp/saved-fault-build \
  --profile loadable_all --app app1 --kind udf --output /tmp/new-fault-run
```

`fault-debug-metadata.py` uses GDB from the existing ARM64 build image. The
runtime runner verifies its ELF/config hashes, uses hardware breakpoints and
reads registers/memory without patching target instructions or state. It stops
at the CPU fault handler, the sender's `mq_send` call, the manager's return from
`mq_receive`, and the instruction immediately before `binary_manager_recovery`.
It requires the same complete message bytes, `BINMGR_FAULT`, correct binary ID,
24-byte receive length, priority 100, matching fault PC and serial diagnostics.
The first two fields are command and binary ID; the message is not a PC dump.

Use `--kind mpu` for the protected read and `--unregistered` for the control.
Run app1/app2 for `loadable_all` and `loadable_apps`, and app1 for `xip_all`:
five app/config combinations, two faults, and two registration states make
20 cases. This port leaves UsageFault disabled, so UDF escalates to HardFault
with `HFSR.FORCED` and `CFSR.UNDEFINSTR`; the runner checks that actual path.
MPU data-access violations use the enabled MemManage handler.
These tests do not invoke the `recover` ioctl or test cleanup/reload outcomes.
