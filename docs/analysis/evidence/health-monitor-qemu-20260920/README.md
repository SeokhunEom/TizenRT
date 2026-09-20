# Preserved QEMU evidence — 2026-09-20

See the [report](../../QEMU_Health_Monitor_Temporary_Validation.md) and
[reproduction instructions](../../../../os/kernel/health_monitor/tests/qemu/README.md).

Results and logs are copied from the completed local validation run. Absolute
source/output paths in JSON and the original `build.py` identify that run's
environment; they are historical records, not portable launch configuration.
`summary.json` likewise records the state before this transfer. The later
`transfer-verification.json` and `transfer-host/` record checks on the destination
branch. No remote CI or physical-board result is implied.

- `qemu-overlay.patch` recreates the 67 final modified/new source files over
  QEMU clock commit `ceef1353723842ed051379d6f370319d4f00dfaf`. It also preserves
  the QEMU-only defconfig and board-device registration changes, without merging
  unrelated QEMU history into the Health Monitor branch.
- `validation-source-delta.patch`, applied after that overlay, and
  `validation-report.md`, copied to the report path, recreate the earlier 68-file
  expanded-test snapshot. The two source manifests identify exact contents.
- `report-before.md` and `validation-report.md` are unmodified historical reports.
  Their statements about unfinished checks and uncommitted files were superseded
  by the current report. Their original temporary-path links are not rewritten.
- `coverage.json` maps the 37 coverage rows to the preserved results.
  Each fatal case retains both its result and serial log.
- Large text logs are gzip-compressed. `compressed-logs.json` gives their original
  SHA-256 values and byte lengths. ELF binaries and host executables are omitted;
  their relevant firmware hashes remain in result records.
- Build results/configurations distinguish the instrumented 16-slot and 4-slot
  firmware from the final firmware with probes disabled. Original build logs are
  preserved, including pre-existing compiler diagnostics.

`archive-manifest.json` hashes the preserved evidence files other than itself
and this explanatory README. It verifies transfer integrity, not independent
attestation of the test outcomes.
