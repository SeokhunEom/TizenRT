# Health Monitor QEMU validation

The `hm_qemu` app exercises real UP ARM threads, VFS/ioctl, lifecycle cleanup,
deadline inspection and intentional PANIC paths. The test-only kernel probes
require LM3S6965, flat mode and no SMP. They are excluded unless
`CONFIG_EXAMPLES_HEALTH_MONITOR_QEMU=y`.

The [validation report](../../../../../docs/analysis/QEMU_Health_Monitor_Temporary_Validation.md)
records results and the boundary between natural time, injected Health Monitor
time, host fixtures and physical-board coverage.

## Recreate the tested QEMU tree

This branch owns the Health Monitor changes. Its QEMU board infrastructure is
older than `codex/qemu-build-test`; do not build its old QEMU defconfig and assume
it reproduces the report. The archived overlay includes all temporary Health
Monitor integration, the test app/runners, the defconfig additions and device
registration in QEMU's existing storage fixture. It preserves the QEMU branch's
separate pthread/mutex fixes and includes no clock change.

Run from this branch's repository root. Choose a new worktree/output path:

```sh
HM_SOURCE="$PWD"
HM_QEMU=/tmp/tizenrt-hm-qemu
HM_EVIDENCE="$HM_SOURCE/docs/analysis/evidence/health-monitor-qemu-20260920"
HM_IMAGE=sha256:8f2d15b7d82cf8c58a9092ec0dcc1ed1bbda9721a6cf19cc832c4eb9a48f8496
git fetch origin codex/qemu-build-test
git worktree add --detach "$HM_QEMU" ceef1353723842ed051379d6f370319d4f00dfaf
git -C "$HM_QEMU" apply --check "$HM_EVIDENCE/qemu-overlay.patch"
git -C "$HM_QEMU" apply "$HM_EVIDENCE/qemu-overlay.patch"
```

This reproduces the archived snapshot, not subsequent edits to this branch.
`final-source-manifest.json` contains SHA-256 values for all 67 overlay files.
To reproduce the exact earlier expanded-test snapshot instead, apply
`validation-source-delta.patch` after the overlay and copy `validation-report.md`
to `docs/analysis/QEMU_Health_Monitor_Temporary_Validation.md`. Its 68 hashes are
in `validation-source-manifest.json`. That historical runner predates the final
timeout wall-time check; use the final runner for new validation.

## Build each configuration separately

The image above contains ARM GCC 10.3.1 and QEMU 2.12.0 with 16 MiB RAM. It must
already exist locally (`--pull=never`). The pinned QEMU tree has Docker recipes
under `tools/qemu-build-test` if a new image is required; record that new image's
identity in new results.

The overlay defaults to Health Monitor enabled, test probes disabled. For the
expanded suite set `CONFIG_EXAMPLES_HEALTH_MONITOR_QEMU=y` in
`build/configs/qemu/build_test/defconfig`; leave the capacity override absent for
the normal capacity of 16. For the separate ENOSPC test also add
`CONFIG_QEMU_HEALTH_MONITOR_CAPACITY=4`.

After every profile change, clean and configure before building:

```sh
docker run --rm --pull=never --platform linux/arm64 --network none \
  -v "$HM_QEMU:/work" "$HM_IMAGE" bash -euc '
    cd /work/os
    make distclean
    cd tools
    ./configure.sh qemu/build_test
    cd ..
    make -j4
    cmp .config ../build/configs/qemu/build_test/defconfig
  '
```

## Execute the matrix

With probes enabled and the default capacity, execute seven nonfatal cases in
one boot, three rounds, followed by ten independently booted fatal cases:

```sh
python3 "$HM_QEMU/tools/qemu-build-test/health-monitor-extended.py" \
  --root "$HM_QEMU" --image "$HM_IMAGE" --output /tmp/hm-expanded-new \
  --case all --rounds 3
for HM_CASE in close equality overdue late wrap stale multi equal far unstable; do
  python3 "$HM_QEMU/tools/qemu-build-test/health-monitor-extended.py" \
    --root "$HM_QEMU" --image "$HM_IMAGE" --output "/tmp/hm-fatal-$HM_CASE-new" \
    --case "fatal-$HM_CASE" || break
done
```

After a separate clean build with capacity 4:

```sh
python3 "$HM_QEMU/tools/qemu-build-test/health-monitor-extended.py" \
  --root "$HM_QEMU" --image "$HM_IMAGE" --output /tmp/hm-capacity-new \
  --case capacity --rounds 3
```

Finally disable `CONFIG_EXAMPLES_HEALTH_MONITOR_QEMU`, remove the capacity
override, and clean/configure/build again. Run normal behavior, measured expiry
and the pre-existing full QEMU workload on that same uninstrumented firmware:

```sh
python3 "$HM_QEMU/tools/qemu-build-test/health-monitor-smoke.py" \
  --root "$HM_QEMU" --image "$HM_IMAGE" --output /tmp/hm-normal-new --mode normal
for HM_MS in 1000 2000; do
  python3 "$HM_QEMU/tools/qemu-build-test/health-monitor-smoke.py" \
    --root "$HM_QEMU" --image "$HM_IMAGE" --output "/tmp/hm-expire-$HM_MS-new" \
    --mode expire --expire-ms "$HM_MS" || break
done
python3 "$HM_QEMU/tools/qemu-build-test/full-set.py" \
  --root "$HM_QEMU" --image "$HM_IMAGE" --output /tmp/hm-full-new --rounds 3
```

Every result directory must be new or empty. Inspect each `result.json` and
exit status; the full-set's accepted result explicitly retains eight known
unsupported-driver failures per round. The archived `clock-check.py` runs inside
the same image, with this QEMU tree mounted at `/work`, and checks RCC/RCC2 and
SysTick against `/work/os/.config`.

## Host tests

The existing 21 ASan/UBSan host variants can also run directly against this
Health Monitor branch in the Linux container. They do not execute real SMP/PM
hardware. See [host-test instructions](../README.md).
