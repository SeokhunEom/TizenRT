# TizenRT diagnostic log catalog

`../catalog.json` describes 34 source-verified diagnostic families and their exact source paths/lines, recognition hints, relevant configuration symbols, analysis guidance and limitations. Source references were checked against the checked-out QEMU port revision recorded in the catalog. The catalog is broader than the structured parser: inclusion does not promise a parser rule, a live capture, or a verified root cause.

The source inventory under `tools/crash-corpus/` surveys available C, C++, header and assembly files in `os/`, `apps/` and `framework/`, including board/driver sources. It records known output APIs, subsystem `*dbg` calls and common logging names, C string fragments, lexical preprocessor conditions and per-file SHA-256. It does not execute a preprocessor or resolve transitive macro routing. Arbitrary vendor wrappers, precompiled libraries, unpopulated submodules, ignored generated files and other formats remain outside that inventory. Buffer formatting with `snprintf`/`sprintf` and raw `putchar`/`write` are not treated as emitted log lines; procfs formats are therefore reviewed separately in the catalog.

## Evidence levels

| Artifact | Establishes | Does not establish |
| --- | --- | --- |
| Catalog source reference | The format/control path exists in this revision | Enabled config, execution, successful parsing |
| Source inventory candidate | A known lexical output-call pattern exists | Reachable emitter or semantic support |
| Source-derived or synthetic fixture | A parser accepts a constructed format | A firmware crash actually happened |
| Captured QEMU log + command/config/image hashes | That emulator run emitted those bytes | Physical board behavior or all config variants |
| Parsed event with source log lines | A rule recognized explicit evidence | Root cause without causal corroboration |
| AI hypothesis with cited lines | An evidence-based investigation direction | A proved bug or successful recovery |

Catalog `runtime_status: source_only` is intentionally conservative. Runtime captures and their expected/observed outcomes belong in the separate crash-corpus manifest. Use that manifest to determine which formats were collected in this task; do not promote every member of a family when one member was exercised.

## Core formats and interpretation

The examples below are **source format strings**, not claimed captures.

| Family | Source format / marker | Interpretation requirement |
| --- | --- | --- |
| ARMv7/8-M and ARMv7-R assertion | `Assertion failed at file:%s line: %d task: %s` | Name can be omitted; preserve file/line and optional task |
| ARMv7-A assertion | `Assertion failed CPU%d at file: %s line %d` then `irq: %d` or `task: %s pid: %d` | Multiple print calls form one physical line; CPU/IRQ differ from task |
| Assertion PC | `Assert location (PC) : 0x%08x` | Assertion site can be downstream of an earlier fault |
| HardFault | `PANIC!!! Hard fault: %08x` | Preserve HFSR plus underlying CFSR evidence |
| MemManage | `PANIC!!! Memory Management Fault at instruction : 0x%08x` | MPU permission/stacking analysis; MMARVALID gates MMFAR |
| BusFault | `PANIC!!! Bus fault at instruction: 0x%08x` | BFARVALID gates BFAR; imprecise faults can have a later PC |
| UsageFault | `PANIC!!! Usagefault at instruction: 0x%08x` | DIVBYZERO, UNALIGNED, NOCP, INVPC, INVSTATE, UNDEFINSTR; STKOF on v8-M |
| Raw Cortex-M status | `CFAULTS`, `HFAULTS`, `DFAULTS`, `BFAULTADDR`, `AFAULTS` | Firmware aliases must be retained; CFAULTS corresponds to CFSR |
| A/R aborts | `PC: %08x DFAR: %08x DFSR: %08x` / `IFAR` / `IFSR` | Architecture-specific decoding, never Cortex-M CFSR masks |
| Register dump | `R0: ...` / `R8: ...`, xPSR/CPSR, EXC_RETURN | Group within the same crash/context, account for split prints |
| Stack region | `User stack:`, `IRQ stack:`, `base`, `size`, `used` | Correct stack context and historical coloration usage |
| Raw stack words | `%08x: %08x ...` | Pointer candidates are not proven call frames |
| Backtrace | `Call Stack:`, `[<0x%p>] %s`, `End of Callstack` | Exact ELF/System.map, architecture and relocation base |
| Stack overflow | `!!! PID (%d) STACK OVERFLOW !!!` | Explicit overflow differs from merely high usage |
| Task table | NAME/PID/PRI/USED/TOTAL STACK/STACK ALLOC ADDR/TCB ADDR/TASK STATE | Header/config-dependent columns, numeric state tied to revision |
| Task control block | State, Flags, Lock count, Timeslice, Waitdog, WaitSem, MsgwaitQ | A single blocked snapshot does not prove deadlock |
| Task history | `Displaying the TASK SCHEDULING HISTORY for %d count` | Ring capacity, ordering and timestamp units |
| IRQ history | `IRQ_TIME IRQ_NUMBER ISR_ADDRESS` | Counters need observation intervals to establish a storm |
| Semaphore history | `IN:INIT AQ:AQUIRED RL:RELEASED WT:WAITING DR: DESTROY UN:UNKNOWN` | PID -1 means ISR; incomplete rings cannot prove full ownership history |
| Workqueue assertion | `Code asserted in workqueue!` / `Running work function is %x.` | Symbolize callback, distinguish worker entry from actual work |
| Heap corruption | `ERROR: Heap corruption detected.` / `CORRUPT NODE` | Detection site need not be corrupting writer |
| Heap negative result | `No heap corruption detected` | Must not match positive corruption diagnosis |
| Double free | `WARNING!! Attempt for double freeing a pointer or releasing an unallocated pointer` | Track first/second free and allocation PCs; metadata corruption is an alternative |
| Allocation failure | `Allocation failed from %s heap.` / requested size/alignment / largest free / total free | Fragmentation and alignment may matter even with sufficient aggregate free bytes |
| Heap summary | `Summary of Heap Usages`, `Nodelist[%d]`, dead-thread allocation | Allocation surviving a thread is not automatically a leak |
| Memory leak checker | `LEAK`, `BROKEN`, `*** %d LEAKS, %d BROKENS.`, `*** NO MEMORY LEAK.` | Preserve negative/partial results, buffer cap and owner/text mappings |
| Stack monitor | PID/STATUS/SIZE/PEAK_STACK/PEAK_HEAP/TIME/THREAD NAME | Separate historical INACTIVE records from live ACTIVE snapshots |
| MPU table | REGION_NO./BASE_ADDRESS/SIZE/STATUS/ACCESS (P/U)/EXECUTE | Enabled-region bounds, privilege and XN must match fault context |
| Error report | `Error_Report pid:%d, taskaddr:%lu` | Identity marker is not a complete diagnosis |
| Binary recovery | `Try to recover fault with binid %d` / `will reload binaries!` | Request alone is not successful recovery |
| Recovery failure | `RECOVERY FAIL, BOARD RESET!!` | Reset request must be distinguished from observed next boot |
| Boot slot validation | `BP set mismatch`, `validation PASS/FAIL`, `Fail to crc check` | Integrity check is separate from application execution |
| Loaded module | `Module:`, filename, entrypt, text/ro/data alloc | Use correct app ELF and load bases, including reload generations |
| Suite summary | `########## %s End [PASS : %d, FAIL : %d] ##########` | Require full intended suite footer; unrelated PASS is not a test result |

Keep source quirks when matching: `largest algined free size` is misspelled in the OOM emitter, `CONFIG_MM_DUMP_CORRPUTED_HEAP` is the actual config spelling, and semaphore `AQUIRED` is the actual legend.

## Coverage beyond crashes

Procfs snapshots include task Name/Type/State/Priority/Scheduler/SigMask, StackBase/StackSize/StackUsed, group/FD/socket data and IRQ counts. Record the command and capture time; reads need not be an atomic global snapshot. A heap dump can intentionally stop to avoid watchdog expiration.

Task-monitor liveness failure writes `REBOOT_SYSTEM_WATCHDOG` when enabled and calls `boardctl(BOARDIOC_RESET, 0)` without a guaranteed dedicated failure banner. An absent banner cannot establish that the monitor did not reset the device. Watchdog upper-half diagnostics describe control operations; actual reset-cause registers and persistence are board-specific. Silent reboot emits a separate periodic reboot message and boot-mode log.

Filesystem, networking, WLAN, DMA and peripheral diagnostics are inventoried by source/subsystem and can be investigated with the surrounding code. Generic keyword matches or emitter names do not provide universal protocol decoding. A recoverable socket or mount error must not be promoted to a kernel crash without evidence. QEMU does not validate physical flash-bus stalls, WLAN behavior, DMA timing or hardware reset sequencing.

Log-dump compression failure includes the wording `The device hang is suspected`. Preserve that uncertainty. Compressed/binary logs require their actual decoder; feeding binary data into a text parser is not successful crash analysis.

## Configuration and capture requirements

Capture the raw serial log, source revision, `.config`, build/image identity, emulator/board, command/case, actual start/end, process return code and explicit observed outcome. Keep multiple boots and crashes as separate episodes. For symbolization also retain exact kernel and app ELFs/System.map plus loaded section addresses. Redact credentials only in a derivative copy while retaining source-line correspondence; do not upload proprietary firmware or logs without the user's authorization.

`CONFIG_DEBUG`, severity and subsystem gates route through `debug.h`, `CONFIG_LOGM`, syslog or low-level output. `CONFIG_ARCH_STACKDUMP` paths can locally redefine debug levels. Configuration names in the catalog are source-relevant symbols, not a complete Kconfig dependency expression; some legacy symbols can be referenced in source without a selectable current Kconfig entry. The inventory records lexical `#if`/`#else` conditions, not whether a specific build emits them.

## Reproduce and inspect the inventory

From the repository root:

```sh
python3 tools/crash-corpus/inventory.py \
  --output tools/crash-corpus/source-inventory.json.gz \
  --summary tools/crash-corpus/source-inventory-summary.json
```

The summary reports available files, candidate call counts, subsystems, emitters and the compressed artifact hash. The full JSON has `entries` and `source_sha256`; each entry has path, line, emitter, literal fragments, format-resolution status, enclosing conditions and config symbols. C escapes are preserved. Search entries by path or literal, then review the source before turning a candidate into a semantic rule. No runtime log is included in the source inventory.
