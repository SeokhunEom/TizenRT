# TizenRT parser coverage and evidence boundaries

This matrix compares the 34 source-verified families in [catalog.json](../catalog.json)
with [the parser](../tizenrt_debug/analyzer.py). It is a feature inventory, not a
claim that every enabled board, configuration or output variant has been exercised.
The repository-wide emitter inventory is broader than these 34 families.

## Levels

- **Structured**: selected records become typed fields, addresses, counts or
  status flags. Adjacent prose and unsupported variants can still need GPT context.
- **Recognized**: a diagnostic becomes an event with its original line; GPT must
  interpret most details against the source, configuration and preceding records.
- **GPT context**: no dedicated parser for the family. Use the original excerpt
  and source catalog. A generic error prefix may still produce an event.

There are **25 structured**, **6 recognized**, and **3 GPT-context** families.
These counts refer to the strongest supported portion of each family; the
limitations column is part of the coverage claim.

| Catalog family | Level | Parsed evidence / remaining interpretation |
| --- | --- | --- |
| `assertion` | Structured | Cortex-M/R file, source line and task; ARMv7-A CPU, task/PID or IRQ; assertion PC and incident boundary. Panic-handler assertion sites are not automatically the originating defect. |
| `cortex_m_hardfault` | Structured | Exception marker, CFAULTS/HFAULTS aliases, FORCED/VECTTBL/DEBUGEVT, register values. Decode accompanying CFSR to identify underlying status flags. |
| `cortex_m_memfault` | Structured | Exception PC, CFSR flags and MMFAR. MMFAR is usable only when MMARVALID is set in the same status record. |
| `cortex_m_busfault` | Structured | Exception PC, precise/imprecise/stacking flags and BFAR validity. An imprecise fault does not identify its originating access from stacked PC alone. |
| `cortex_m_usagefault` | Structured | UNDEFINSTR, INVSTATE, INVPC, NOCP, STKOF, UNALIGNED and DIVBYZERO flags. Flags describe reported exceptions, not a confirmed code-level cause. |
| `arm_abort` | Structured | ARMv7-A PANIC headers and ARMv7-R Data/Prefetch abort PC, DFAR/DFSR and IFAR/IFSR. ARM A/R fault-status bit interpretation remains architecture-specific GPT context. |
| `register_dump` | Structured | Packed R0/R8 rows, named R0–R15, PC/LR/SP aliases, xPSR/CPSR, masks, EXC_RETURN and banked SP/LR/SPSR. Missing registers stay missing; unrecognized architecture variants remain raw. |
| `stack_dump` | Structured | Stack headers, base/size/used, SP and raw words with original addresses. Raw memory words are never promoted to verified call frames. |
| `backtrace` | Structured | Explicit `[<0x...>]` frames and optional emitted symbol hints. General unwinder prose, symbol misses and nonmatching frame formats require excerpts. |
| `stack_overflow` | Structured | `!!! PID (...) STACK OVERFLOW !!!`, out-of-range SP and CFSR STKOF. StackUsed/StackSize snapshot values are preserved without assuming a fault. |
| `task_dump` | Structured | Task table PID/priority/stack allocation/state number and TCB fields. Numeric task-state enums require the matching configuration. |
| `scheduler_history` | Structured | Ring records with timestamp, task name (including spaces), PID, priority and TCB. Order is newest-first; time units and scheduling causes require firmware context. |
| `irq_history` | Structured | IRQ ring records and `/proc/irqs` IRQ/count/name rows. A large count alone does not establish an interrupt storm. |
| `semaphore_history` | Structured | Semaphore status, task, semaphore/TCB address and PID. Holder-exhaustion/destruction messages are recognized. A WT record alone does not establish deadlock. |
| `workqueue_context` | Structured | Workqueue assertion context and running function address, eligible for optional symbol lookup. The scheduled work's creator is not inferred. |
| `heap_corruption` | Structured | Positive/negative checker results, context records and heap dumps. Node prose is preserved; the reported node does not identify the writer. |
| `double_free` | Structured | Invalid-free warning, previous/current free caller and allocation caller/task/PID. Preserve the emitter's ambiguity between double free and an unallocated/arbitrary pointer. |
| `allocation_failure` | Structured | Requested size/alignment, caller, largest free/aligned block and total free bytes. Exhaustion, fragmentation, alignment and allocation limits remain competing explanations. |
| `heap_usage` | Structured | Heap totals/peak/free, node ownership, free buckets, per-thread usage and `free` totals. Region-header prose and dead-thread ownership interpretation remain GPT context. |
| `procfs_snapshot` | Structured | Name/type/state/priority/scheduler/stack fields and actual `ps` columns including WAIT INFO. CPU-load snapshot variants and unnamed standalone fields may remain raw. |
| `task_monitor` | GPT context | Registration/range diagnostics and silent liveness decisions require original text plus source/configuration. A missing heartbeat message is not proof of failure. |
| `watchdog_reboot` | Recognized | Silent-reboot request, watchdog countdown and selected watchdog failures. A request/countdown is distinct from a later observed boot. Board-specific reason codes remain GPT context. |
| `binary_recovery` | Recognized | Recovery request, loader status, successful reload and reset request. Preserve both failed and later successful attempts; do not infer recovery completion from a request. |
| `binary_bootparam` | Recognized | BP mismatch, set validation PASS/FAIL, CRC check failure and BP index markers. Slot IDs, version compatibility and partition transitions require surrounding records. |
| `loadable_mapping` | GPT context | Module filename, entrypoint, text allocation and other relocation details require excerpts. Automatic load relocation is not implemented; symbol lookup requires the matching artifact/address space. |
| `filesystem` | Recognized | Generic ERROR/WARNING/FAIL diagnostic events preserve mount/procfs/storage text. There is no universal errno, filesystem or device-specific decoder. |
| `network_driver` | GPT context | Catalog macro names identify emitter routes, not runtime signatures. Interface statistics and board-specific driver messages remain excerpts for GPT interpretation. |
| `log_capture` | Recognized | Compression failures, suspected log-dump hang, heap-output cutoff and size markers. No compressed-stream decoder; absent output does not establish absence of a fault. |
| `debug_routing` | Recognized | ANSI and numeric timestamp removal for matching; common lowercase underscore function prefixes. Raw lines stay unchanged. LOGM/board-specific/interleaved prefixes are not universally normalized. |
| `kernel_test_result` | Structured | Per-case PASS/FAIL, failure source line and completed suite pass/fail counts. PASS inside a test name/message is not a firmware crash; missing suite end is not suite success. |
| `memory_leak_checker` | Structured | LEAK/BROKEN records and summary counts; explicit NO MEMORY LEAK remains a negative observation. Reachability/ownership and a limited scan require review. |
| `stack_monitor` | Structured | Header-driven ACTIVE/INACTIVE rows, stack/peak heap/time/name where configured. Configuration-dependent omitted fields remain absent. |
| `mpu_regions` | Structured | Region, base, reported size, enabled state, permissions and execute flag. Disabled-region derived size is not exposed as an effective mapping. |
| `error_report` | Structured | PID and decimal task-address marker. Error-report payload and persistent crash artifact decoding remain separate work. |

Additional parser observations include QEMU fatal/architectural lockup, explicit
reproduction markers, shell prompts, unavailable diagnostic configurations, and
early kernel heap initialization as a boot marker. These are useful for incident
boundaries but do not add catalog-family coverage. `System Information:` from the
`sysinfo` command is deliberately not a boot marker. `boot_banner_count` includes
the source-specific early-kernel-heap startup marker as well as textual banners.

## Source-derived fixtures versus actual runtime

[test_analyzer.py](../tests/test_analyzer.py) uses explicit source-format fixtures
for supported variants and adverse boundaries; these are not runtime captures.
[test_symbolize.py](../tests/test_symbolize.py) verifies explicit ELF/map lookup,
address roles, error handling and preservation of the source report. Map results
are nearest-symbol candidates without verified symbol sizes or build identity.

[test_corpus.py](../tests/test_corpus.py) independently checks the packaged
[QEMU examples](../examples/qemu-armv8m/manifest.json): **24 real QEMU sessions**
(23 diagnostic sessions plus a separate full `kernel_tc` session) with raw-byte hashes and
capture receipts. It parses saved logs; running these tests does not rerun QEMU.
It checks these concrete outcomes:

1. Receipt byte sizes/hashes, expected diagnostic markers, and capture timeouts.
2. Exact one-based original-line correspondence for every parsed event.
3. Normal boot, task, allocator, stack-monitor and history output without invented
   failures. The network interface example stays GPT context.
4. Assertion, UsageFault, BusFault, forced HardFault and MemManage status semantics.
5. Valid BFAR/MMFAR for data faults and invalid MMFAR for the execute fault.
6. Allocation/invalid-free/heap-corruption records and their caller evidence.
7. Both STKOF and accompanying heap-corruption observations in the stack sample.
8. Guest reboot requests ending incidents before the next boot's records.
9. Scheduler task names containing spaces and semaphore status records.
10. Completed `Kernel TC End [PASS : 459, FAIL : 0]`, with no parsed crash or
    test-failure records, from its separately captured session.

The 23-session diagnostic set does **not** establish physical-board behavior, runtime
coverage of all 34 families, ARMv7-A/R execution, Binary Manager recovery,
architectural lockup, or a full `kernel_tc` pass. The separately archived 24th
session provides the full `kernel_tc` receipt for this exact QEMU build (459 passes,
zero fails); ARMv7-A/R, Binary Manager and physical-board execution remain outside
that evidence.
The first temporary double-free capture lacked allocator messages because its
debug gate was disabled; the final archived capture includes the warning after
the gate was enabled. A missing diagnostic is never relabelled a reproduced fault.

To rerun the packaged regression:

```sh
cd plugins/tizenrt-debug-analyzer
python3 -m unittest discover -s tests -v
```

`TIZENRT_CORPUS_DIR=/absolute/archive` selects an explicit corpus directory.
An explicitly selected incomplete archive fails. If the default corpus is absent
from a distribution, the runtime-corpus test class is explicitly skipped rather
than claiming that the archived runtime was checked.

## MCP report sections and unknown lines

The analyzer emits `unknown_lines` (not `unparsed` or `unmatched`). Each item has
`line` and `raw`, and the list retains the first **100** unclassified nonempty
records. `summary.unclassified_lines` counts all such records. This bounded list
is a navigation aid; GPT must use `get_log_excerpt` on the original log for lines
beyond it or for the surrounding context of a finding.

Line references follow physical LF boundaries, matching editor and `nl -ba` line
numbers. Trailing CR terminators are removed, including UART CR CR LF sequences;
they do not create extra empty lines. CR-only captures use CR boundaries. Interior
control characters remain in the evidence. The parser and MCP excerpts share
the `log_lines()` helper so a cited line selects the same content in both views.
Read files as bytes before decoding when calling the Python API. `Path.read_text()`
uses universal-newline conversion and can turn UART CR CR LF into two LF lines
before the parser sees it. The CLI and corpus tests preserve the original bytes:

```python
from pathlib import Path
from tizenrt_debug import analyze

path = Path("kernel.log")
report = analyze(path.read_bytes().decode("utf-8", errors="replace"), source=str(path))
```

The other list sections are `events`, `incidents`, `registers`, `tasks`, `heap`,
`stack`, `tests`, **`history`**, `findings`, and `limitations`. `history` contains
scheduler, interrupt and semaphore records; clients should expose it alongside
the other sections. `summary` is an object. Optional `symbolization` is also an
object and preserves evidence-line references.

Incident boundaries use explicit fault/assertion starts, reboot requests, known
boot markers and shell prompts. UART/SMP interleaving, omitted records or a
truncated capture can still make the boundary ambiguous. Findings retain their
evidence and distinguish a reported fault from a hypothesis about its cause.
