# Standards and design review

Fixed main three-dot baseline: 080481ee8bea3511ec5b0b8baac6d0a713f571aa...cc00205afc56e938132403dc3d3aae65db8e8cb6. Feature attribution baseline: 79afdbf91a9f0005fdf4e92a83f420fe46487b3a..cc00205afc56e938132403dc3d3aae65db8e8cb6.

## Substantive finding

P2 test-tool correctness / side effects in assertions: tools/qemu-armv8m-health-monitor/fault-message.py:67,86. Required socket ACK consumption and breakpoint request execution occur inside Python assert expressions. CPython removes the entire expressions under -O / PYTHONOPTIMIZE. Bounded AST-isolated reproduction called the real Remote.breakpoint method under optimize=0 and optimize=1: requests were ["Z1,1234,2"] and [] respectively. No QEMU or target was executed. Repro data is assert-side-effect-repro.json in this directory. This means required debugger protocol behavior disappears, and assertions checking firmware identity, expected fault and exact message bytes disappear too. Full optimized runner may abort early on unconsumed ACK; no actual recovery execution or false pass is claimed. Call protocol methods unconditionally and check their results explicitly, plus replace verdict asserts with explicit errors; minimal defensive alternative is reject sys.flags.optimize before QEMU launch. Existing normal-mode runs are not invalidated by this finding.

## Optional design consideration, not violation

Production timeout diagnosability: os/kernel/health_monitor/health_monitor.c:578-604. The monitored PID/deadline are copied only for CONFIG_EXAMPLES_HEALTH_MONITOR_QEMU. Production writes reason62 then PANIC, which on ARMv7-A diagnoses the currently executing CPU0 task rather than necessarily the expired task. This limits attribution among many registered tasks. Plan docs/HealthMonitorImplementationPlan.md:196 explicitly accepts existing PANIC only and excludes an extra dump framework, so this is not a missing promised feature or merge blocker. If product needs fault attribution, copy a bounded PID/deadline scalar record under the existing lock and report outside it; do not retain/dereference TCB after unlock, add callback frameworks or a full new dump system.

## Positive architecture assessment

No substantive mandatory CodingStyleGuide breach identified in reviewed product code. M09 public/internal header split is present. R07/R15/R16 recommendation differences are shared with existing kernel/driver conventions and are not operational defects. Header-only address seam health_monitor_state is explicitly requested by plan section4.1, not an invented abstraction.

Heap + lazy KICK + one lock + nonblocking atomically published hint implement stated O(1) KICK/tick and PM requirements. The static allocation, single lifecycle cleanup shared by STOP/exit/restart/release, by-value ioctl avoiding user-pointer copying, lack of per-FD registration state, unlock-before-PANIC, and isolated QEMU Kconfig guards are appropriately restrained C design. Replacing heap with per-tick full scan, merging real deadline with check_at, or deleting publication protocol for simplicity would break stated cost/concurrency contracts.

No source modifications, build, commit or push were performed. This independent Standards/design axis does not claim physical SMP, PM, watchdog or target timing proof; parent Spec review handles functional findings separately.
