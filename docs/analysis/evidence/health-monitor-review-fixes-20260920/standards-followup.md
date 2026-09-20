# Health Monitor Standards follow-up

Read-only current tracked-diff review in `/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor`. No product edits or commits performed.

## Result

The previous Python optimization P2 is resolved in the inspected files. `fault-message.py` no longer places required protocol operations or verification in `assert`: ACK consumption, breakpoint requests, checksum/length checks, metadata hashes, configuration checks, fault identity, exact message equality and the pre-recovery stop conditions now execute through explicit conditionals and exceptions. `fault-debug-metadata.py` also retains its metadata completeness, unique call-site, stack-buffer-offset and context-format checks under optimization.

The exact-read `Remote.receive(size)` helper is appropriately scoped: TCP can split the checksum, and both the checksum and ACK have the same read-until-complete/EOF requirement. It is not an unnecessary protocol framework or generalized validation layer.

`test-fault-message.py` exercises actual Remote methods and unittest verdicts, not optimized-away assert statements. Independently ran `PYTHONDONTWRITEBYTECODE=1 python3 [mode] tools/qemu-armv8m-health-monitor/test-fault-message.py` for normal, `-O`, and `-OO`; all modes passed 6 tests each (18/18). These cover add/remove breakpoint requests, ACK consumption, negative breakpoint response, negative ACK, incorrect/truncated checksum and short memory replies. No bytecode was written into the worktree. This is host protocol evidence, not a new QEMU or physical-board run.

The optional timeout-attribution improvement is implemented with local PID/deadline scalar copies under the registry lock and low-level output after unlock, before PANIC. The freed-TCB interleaving timer test checks copied values. The existing heap, publication protocol and lifecycle design remain intact. Diagnostic output remains conditional on DEBUG_ERROR and architecture low-level support, as the source explicitly indicates.

No new confirmed Standards/code-quality defect was found. No new speculative abstractions or runtime allocations were introduced into the Health Monitor core.

PM elapsed-time correctness remains a separate Spec axis. The suggested mid-transition non-idle START/KICK race was not established: pm_idle holds the global critical section and sched_lock before confirming active secondary CPUs are idle (pm_idle.c:172-199,597-610); scheduler ready-list changes require that global critical section, and irq_csection.c:346-379 blocks acquisition by another CPU and prevents preemption. No current RTL/PM callback calls HM START/KICK. Therefore this is not a concrete defect in the current integration. The current health_monitor_start implementation itself does not explicitly reject idle PIDs; absence of current PM callback registrations should not be described as an API-enforced idle prohibition. This standards result does not by itself close PM functional validation or physical SMP/PM/watchdog proof.

## Reviewed script identity

| File | SHA-256 | Remaining Python assert lines |
|---|---|---|
| `tools/qemu-armv8m-health-monitor/fault-message.py` | `ff0b6141cd891cca19d9f80ca1981ead6fb03aa07a943872dc07a767832d4e7f` | [] |
| `tools/qemu-armv8m-health-monitor/fault-debug-metadata.py` | `97c17bc01ec62cfed7432164afb8df0c77c6147dfd817e8be4e59ffa775e076e` | [] |
| `tools/qemu-armv8m-health-monitor/test-fault-message.py` | `11d3a7f97aa58733cd0fb410695c2ac1b1eebcefbf2ab460b1845f1c4af881ba` | [] |
