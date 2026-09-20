# Latest-source preboard validation evidence

Recorded on 2026-09-20. See the [Korean report](../../Health_Monitor_Preboard_Validation.md) and [summary](summary.json).

## Identity and contents

- Health Monitor source: `6d4deed26f836378d961561c50617cde1a8ce00a`.
- QEMU port base: `5c0120f685ff47ab17f7ec26c0554965e312bc27`, plus the tracked integration patch and 74 current files in `qemu-health-source-manifest.json`.
- `rtl-{off,on,test}`: effective config, build log, artifact checks, ELF sizes/symbols, DWARF TCB sizes, FIP inventory.
- `qemu-*-build`: config, fresh ELF hash, build log, symbols and fault metadata where applicable.
- Runtime folders: result JSON, compressed serial/runner logs and GDB RSP trace where generated.
- `qemu-matrix.json`: 10 successful builds and 53 successful runtime scenarios. Absolute result paths identify original local outputs; their basename directories are archived here.
- `diagnosis` and `qemu-loadable_all-production-reset-1000`: the preserved invalid reset expectation for a HALT image, resolved by checking expiry on that same ELF. It is outside the final successful matrix.
- `evidence-index.json`: SHA-256 and byte length of every other archived file. Gzip uses mtime 0.

ELFs, flash packages, QEMU RAM files and compiler output trees are not duplicated in Git. Raw outputs remain at `/private/tmp/hm-preboard-20260920-8_tdqiqr`; temporary local storage is not a durable binary download. Their hashes and inspection results are retained here. Rebuild if temporary artifacts are gone. Timestamps/debug paths can change hashes across reproductions.

Final packaging compared the original QEMU HEAD/status and all 87 recorded file hashes, checked 74 current source hashes, and tied every runtime ELF/config to a fresh build. `plan.json` records the initial no-commit request; a later instruction authorized documentation/evidence commit and push. The execution source remains the pinned HEAD above.

## Reproduction

The [scripts](reproduction) preserve local orchestration and inspection, including the diagnostic resume step. They contain absolute paths and older config-only archive references. They are records of this run, not a portable test framework. Adapt paths to a **new scratch directory**, use the complete configs archived here, and do not blindly rerun completed-directory/resume scripts. `resume_qemu.py` only documents the expectation correction.

1. Export the pinned Health Monitor tree to three fresh RTL directories. Follow [board-validation.md](../../../health-monitor/board-validation.md): configure off/on/test and build with the recorded image. Apply `verify_rtl.py` and `measure_rtl.py` to that output layout.
2. Create a detached checkout of the QEMU port base. Apply **`qemu-latest-overlay.patch` once**: it is the full tracked diff and already includes `qemu-existing.patch`. Copy all paths from `qemu-health-source-manifest.json` out of the pinned Health Monitor tree, including untracked test directories. `prepare_qemu.py` records equivalent preparation from the then-live sibling worktree.
3. Copy each archived effective config to its scratch recipe defconfig. Use Homebrew Bash and the port's clean/reconfigure/build workflow: `dbuild.sh 5 qemu-armv8m PROFILE 1`, omitting 5 only for the first unconfigured build. Preserve each fresh firmware and config. Complete configs matter because this configure path does not synthesize omitted Kconfig defaults.
4. Use the [test runners](../../../../tools/qemu-armv8m-health-monitor/README.md) with the cases/rounds in `qemu-matrix.json`, matching the live scratch firmware and using new output directories. HALT images use `expiry`; AUTORESET hello uses `reset`. The OFF helper is `reproduction/qemu_off.py`.
5. For fault images, run `python3 -O fault-debug-metadata.py BUILD_DIR`, then `python3 -O fault-message.py` for every supported app/kind/registration tuple. Stop before `binary_manager_recovery()`; recovery/reload and long unload tests are excluded.
6. Recheck source/ELF/config identity and aggregate counts with `verify_qemu.py` and `package_evidence.py`, adapting paths and source identities deliberately. They inspect existing results and do not replace runtime execution.

The Docker tags are local ARM64 images; their IDs are in the report. Host models, QEMU execution and physical RTL8730E execution are separate evidence classes. Actual board PM, SMP timing, watchdog reset and reboot-reason retention remain unrun.
