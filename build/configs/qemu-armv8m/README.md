# QEMU ARMv8-M MPS2-AN505

This target boots TizenRT on QEMU's `mps2-an505` Cortex-M33 machine. The
public target name remains `qemu-armv8m`; for its relationship to the legacy
LM3S6965 target, see [QEMU Target Roles](../qemu-targets.md).

## Supported configurations

The supported set is exactly these four configurations.

| Configuration | Build model | Package contract |
| --- | --- | --- |
| `hello` | Flat kernel image with TASH and `kernel_tc` | Kernel image only |
| `loadable_all` | Protected kernel with a loadable ELF application | Kernel image and `app1` |
| `loadable_apps` | XIP kernel with a loadable ELF application | Kernel image and `app1` |
| `xip_all` | XIP kernel with XIP common/application packages | Kernel image, `common`, and `app1` |

Every supported layout has one application package: `app1; app2 and later are unsupported`.
The board loader owns the package addresses and validates each package before
starting it. For `xip_all`, the runner loads `common before app1`; omitting or
rejecting `common` prevents the `app1` start path.

## Build

From the repository root, configure one of the four configurations in `os`.
Run `make distclean` before switching configurations because `configure.sh`
does not overwrite an existing configuration.

```sh
cd os
make distclean
./tools/configure.sh qemu-armv8m/hello
make
```

Replace `hello` with `loadable_all`, `loadable_apps`, or `xip_all` as needed.
The kernel image is `build/output/bin/tinyara` relative to the repository root.
Loadable configurations also produce `build/output/bin/app1`; `xip_all` also
produces `build/output/bin/common`.

## Validate with the QEMU runner

Use the repository runner for every ARMv8-M runtime check. It constructs the
board-specific QEMU command, waits for a fresh TASH prompt, sends `kernel_tc`,
and writes a serial log plus a JSON result.

```sh
python3 .github/scripts/qemu-armv8m-kernel-tc.py --config hello --timeout 1200
python3 .github/scripts/qemu-armv8m-kernel-tc.py --config loadable_all --timeout 1200
python3 .github/scripts/qemu-armv8m-kernel-tc.py --config loadable_apps --timeout 1200
python3 .github/scripts/qemu-armv8m-kernel-tc.py --config xip_all --timeout 1200
```

A positive run succeeds only when the terminal testcase result has `PASS > 0`
and `FAIL : 0`. Its result JSON records:

```json
{
  "status": "pass"
}
```

The runner's default artifacts are
`build/qemu-armv8m/<config>-kernel-tc.log` and the adjacent result JSON. Supply
`--log` and `--result` when retaining a named run artifact.

## Package-rejection checks

The board exposes `QEMU_LOAD_REJECT common` and `QEMU_LOAD_REJECT app1` for
the corresponding rejected slots. A negative run must require the appropriate
marker with `--expect-reject` and must reject `QEMU_APP1_STARTED` with
`--forbid-marker`; an expected rejection is not a successful application
launch. The successful rejection contract records `"status": "expected-rejection"`
in the result JSON.

The CI matrix covers corrupt-common, omitted-common, corrupt-app1, and
oversized-app1 through this runner interface. It also verifies the generated
`xip_all` layout before runtime validation.

## Local and CI validation boundary

Local QEMU runtime proof is limited to `hello` and the existing
`tizenrt/tizenrt:2.0.1-arm64-local` image. Loadable, XIP, and negative-package
runtime proof is CI-only after explicit candidate commit/push authorization;
do not infer those results from a local macOS build.

The CI contract is maintained in `.github/workflows/qemu-armv8m.yml`. Keep its
`ubuntu-24.04` runner, `tizenrt/tizenrt@sha256:` image digest, and GitHub
actions at a reviewed full commit SHA. Update a pin only after reviewing the
replacement and preserving the workflow's recorded QEMU package/machine
evidence.

Positive artifacts are stored at
`build/qemu-armv8m/ci-artifacts/<config>/`, including `serial.log`,
`result.json`, `build.log`, `defconfig.sha256`, and the image/QEMU version
records. Negative cases use
`build/qemu-armv8m/ci-artifacts/negative-<case>/`; the XIP positive artifact
also contains `xip-layout-report.json` and the program/section readelf output.

## Notes

- The runner and board source are the layout authority. Do not copy package
  addresses or raw QEMU loader commands into this guide.
- The timer testcase allows one scheduler tick because remaining timer time is
  tick-granular on this target.
