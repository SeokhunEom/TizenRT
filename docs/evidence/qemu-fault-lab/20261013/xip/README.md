# XIP dependency evidence — 2026-10-13

- Model: MPU access errors with HardFault protection enabled. This is not a
  Realtek SPIC model or a non-responding bus simulation. No T32 was connected.
- QEMU 11.1.2, MPS2-AN505, GCC 10.3, flat/FPU off/SMP off.
- `qemu/results.json`: all four cases, 25 fresh processes each (100/100).
  Cases 1/2 PASS means the expected architectural lockup was observed;
  cases 0/3 PASS means guest reset, retained diagnostics and a new TASH command.
- `qemu/case-*`: unmodified representative serial, exception trace, stderr,
  and result. All run verdicts are retained, not 100 duplicate traces.
- `faultlab-regression`: existing cases 1..6, three times each (18/18).
- `qemu/kernel-network-*`: network 161/0 and kernel 459/0 on the final lab ELF.
  The runner's `config: hello` selects its flat protocol; the actual firmware
  config is `qemu/effective.config` (`ARMV8M_XIP_FAULT_LAB=y`).
- `rtl8721csm`: successful cross-build, config, ELF layout and RAM disassembly.
  Hardware execution, bootloader retention, reset behavior and T32 access are
  NOT validated. The original hello SE setting required TrustZone; this
  dedicated TrustZone-off recipe disables SE and related HW TLS settings.
- ELF binaries are not committed. SHA-256 in metadata identifies each tested
  build. `head` is the parent commit during work; working-source hashes and
  effective configs identify these changes before this commit was created.
- Build log copies remove trailing whitespace; runtime logs retain raw bytes.
  Existing unrelated RTL vendor warnings remain visible in its build log.
- `sha256.json` checks the files in this evidence directory.

See [test instructions](../../../../Human/XIP_Fault_Dependency_Lab.md).
