# QEMU ARMv8-M MPS2-AN505

This target boots TizenRT on QEMU's `mps2-an505` Cortex-M33 machine. The
public target name remains `qemu-armv8m`; for its relationship to the legacy
LM3S6965 target, see [QEMU Target Roles](../qemu-targets.md).

## Supported configurations

The supported set is exactly these four configurations.

| Configuration | Build model | Package contract |
| --- | --- | --- |
| `hello` | Flat kernel image with TASH and `kernel_tc` | Kernel image only |
| `loadable_all` | Protected kernel with loadable ELF applications | Kernel image, `common`, `app1`, and `app2` |
| `loadable_apps` | Protected kernel with loadable ELF applications | Kernel image, `common`, `app1`, and `app2` |
| `xip_all` | XIP kernel with XIP common/application packages | Kernel image, `common`, and `app1` |

`app1 and app2 are supported by loadable configurations`; `xip_all` uses one
XIP application. The board flash adapter registers the RAM-backed MTD and
partition map; Binary Manager owns package discovery, validation, loading, and
boot-parameter recovery. For `xip_all`, the runner stages `common before app1`;
omitting or rejecting `common` prevents the `app1` start path.

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
The generated `os/.config` is used when it matches the requested recipe mode,
so menuconfig changes to the selected recipe are reflected in the runner. The
kernel image is `build/output/bin/tinyara` relative to the repository root.
Loadable configurations also produce `build/output/bin/app1`; `xip_all` also
produces `build/output/bin/common`.

`make download` selects the matching runner recipe automatically: `xip_all` for
`CONFIG_XIP_ELF=y`, `loadable_apps` for `CONFIG_XIP_KERNEL=y`, `loadable_all`
for the remaining separated build, and `hello` for the flat build.

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

The Binary Manager reports a rejected slot with
`binary_manager_load: Invalid Header data, name : common` or
`binary_manager_load: Invalid Header data, name : app1`. A negative run must
require the appropriate diagnostic with `--expect-reject` and must reject the
corresponding Binary Manager success diagnostic with `--forbid-marker`:
`binary_manager_load: common Header Checking Success` for common or
`binary_manager_load: app1 Header Checking Success` for app1. This ensures
the invalid package is not replaced by a successfully loaded alternate
package; an expected rejection is not a successful application
launch. The older `QEMU_LOAD_REJECT` strings belong to
the board's non-Binary-Manager loader unit fixture, not these runtime recipes.
The successful rejection contract records `"status": "expected-rejection"` in
the result JSON.

The CI matrix covers corrupt-common, omitted-common, corrupt-app1, and
oversized-app1 through this runner interface. It also verifies the generated
`xip_all` layout before runtime validation.

## SRAM and persistent A/B state

The QEMU port reserves the upper 512 KiB of the MPS2-AN505 SSRAM as a second
heap. Kernel code stays below that boundary. The loadable and XIP layouts use
three heap regions: 4 MiB of kernel RAM, 8 MiB of loaded-application RAM, and
512 KiB of SSRAM. The flat `hello` layout uses 12 MiB of main RAM plus the same
512 KiB SSRAM heap. This keeps the heap indices aligned with the BK7239N-style
main/secondary-memory model while leaving room to grow app and XIP recipes.

For loadable and XIP configurations, the runner stages a 16 MiB persistent
QEMU state image containing the RAM-backed flash area and two package slots.
Pass `--state-image build/qemu-armv8m/qemu.state` to retain the boot parameter
across invocations. A failed run can select the alternate slot when
`--max-reboots` is greater than zero. Delete the state image after rebuilding
packages when a fresh image is required.

## Local and CI validation boundary

Local runtime evidence includes `hello`, `loadable_all`, `loadable_apps`, and
`xip_all` using the ARM64 Docker image and the persistent-state runner. The
checks proved TASH boot, Binary Manager discovery, app loading, and XIP package
placement. On 2026-07-25, clean builds followed by fresh runner invocations
completed with `PASS : 459, FAIL : 0` for `hello` and
`PASS : 447, FAIL : 0` for each loadable/XIP configuration. These results are
local QEMU software-path evidence, not hardware-board validation.

`hello` keeps both priority inheritance and Binary Manager disabled, so it does
not compile semaphore-holder tracking. The PI-disabled `loadable_all` and
`loadable_apps` recipes use `CONFIG_SEM_PREALLOCHOLDERS=16` for Binary Manager
holder recovery. `xip_all` uses the same pool size for priority inheritance and
Binary Manager. The preallocated holder pool is a finite holder-accounting
resource; size it for the maximum concurrent task/semaphore holder pairs.

CI remains the reproducible positive/negative matrix after explicit candidate commit/push authorization. It also verifies the generated XIP layout and
package rejection cases, so local evidence and CI evidence must remain separate.

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
