# Validation record — 2026-10-16

Base: `codex/qemu-armv8m-kernel-tc` at
`5c0120f685ff47ab17f7ec26c0554965e312bc27`.
Worktree: `codex/tizenrt-log-analyzer`.

## Runtime evidence

- ARM64 Docker build of `qemu-armv8m/log_analyzer` succeeded using GCC 10.3.1.
- 23 diagnostic cases ran on QEMU MPS2-AN505/Cortex-M33; all expected diagnostics
  were observed, without observation timeout. Twelve fatal cases also showed
  a reboot request and a later guest TASH prompt.
- The same ELF ran `kernel_tc`: 459 PASS / 0 FAIL, 128.29 seconds. The parser
  recognized 459 individual passes and the footer, with zero incidents.
- After correcting the collector's stackmonitor observation window, a separate
  run recorded three tables over 2.518 seconds after command submission. The
  original single-table capture was preserved.
- Raw hashes, config, symbols, input source snapshots and invocation/termination
  metadata were checked with `tools/crash-corpus/verify.py` for all three runs.
- The plugin includes 24 unmodified raw logs in `examples/qemu-armv8m`, with a
  hash manifest. Full collector provenance remains under `tools/crash-corpus`.

## Software checks

- 76 Python unittest cases passed: parser, symbol lookup, MCP, real corpus and
  independent physical-line/evidence checks. Two collector recognition tests
  additionally check real output and command-echo negatives.
- Actual ELF/addr2line CLI lookup resolved divide-by-zero PC `0x10053078` to
  `crashprobe_main` at the captured source line 63. On this Mac the explicitly
  supplied temporary addr2line adapter ran the build image's GNU ARM addr2line
  against a read-only bind mount. The plugin itself bundles no toolchain.
- Official Python MCP SDK 1.30.0 completed initialize, five-tool discovery,
  log-file analysis, exact excerpt retrieval, catalog lookup and ping over stdio.
- Portable plugin and MCP JSON passed their published Agent Plugins 1.0 JSON
  schemas. Skill and agent metadata were parsed as YAML.
- The private ZIP was extracted outside the repository. All 76 tests and the
  official MCP client smoke passed again from the extracted package; the CLI
  also produced a Markdown report from its bundled actual divide-by-zero log.
- The installed Codex CLI discovered `tizenrt-debug-analyzer@tizenrt-local` using
  command-local marketplace configuration. No user-global marketplace/plugin
  installation or account upload was performed.

## Interpretation boundaries

34 source-backed diagnostic families are cataloged: 25 have structured parser
coverage, six have diagnostic recognition, and three require GPT interpretation
of source context and raw excerpts. These counts are capabilities, not a claim
that every configuration/architecture/print site was executed.

The lexical source inventory is broader than the parser and includes normal
application output. It records source hashes and preprocessor conditions without
evaluating all configurations. Build identity is established by captured input
hashes and ELF hash; a supplied arbitrary ELF is never assumed to match a log.

Hardware, real watchdog/XIP bus behavior, ARM-A/R runtime, loadable recovery,
binary logdump decoding, remote ChatGPT hosting, and actual host GPT invocation
of an installed plugin remain outside this validation. GPT interpretation is
provided through the skill; no separate model API result is claimed.
