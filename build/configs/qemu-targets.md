# QEMU Target Roles

TizenRT has two QEMU board families under `build/configs/`: `qemu` and
`qemu-armv8m`. They are complementary targets, not replacements for each other.

## Summary

| Target | Emulated machine | CPU architecture | Main role |
| --- | --- | --- | --- |
| `qemu` | `lm3s6965evb` | Cortex-M3, ARMv7-M | Existing LM3S6965 regression and filesystem/network-oriented QEMU coverage |
| `qemu-armv8m` | `mps2-an505` | Cortex-M33, ARMv8-M | ARMv8-M port validation, MPU/FPU paths, and loadable/XIP app-package coverage |

Use `qemu-armv8m` for new ARMv8-M work. Keep `qemu` for the existing
LM3S6965/ARMv7-M coverage and for configurations that do not exist in
`qemu-armv8m`.

## Existing `qemu`

The `qemu` target uses the legacy `lm3s6965-ek` board configuration and boots
with QEMU's `lm3s6965evb` machine:

```sh
qemu-system-arm -M lm3s6965evb -kernel ../build/output/bin/tinyara -nographic
```

Its configurations cover the existing LM3S6965 test matrix:

- `build_test`
- `smartfs`
- `tc_16m`
- `tc_16m_grpc`
- `tc_1m`
- `tc_64k`

This target is still useful when validating ARMv7-M behavior, LM3S6965 board
logic, old memory-size variants, SmartFS, network/grpc paths, or existing
regressions that are tied to the `qemu` config set.

## New `qemu-armv8m`

The `qemu-armv8m` target uses a dedicated MPS2-AN505 board and chip path and
boots with QEMU's `mps2-an505` Cortex-M33 machine. Build the selected
configuration, then use the tested runner rather than assembling a raw QEMU
command:

```sh
python3 .github/scripts/qemu-armv8m-kernel-tc.py --config hello --timeout 1200
```

Its configurations cover ARMv8-M and app-package layouts:

- `hello`: flat build with TASH and `kernel_tc`
- `loadable_all`: protected build with a loadable `app1` package
- `loadable_apps`: XIP kernel with a loadable `app1` package
- `xip_all`: XIP kernel plus XIP `common` and `app1` packages

Each supported layout uses only `app1`. For `xip_all`, the runner loads and
validates `common` before `app1`; package placement remains a board/runner
contract rather than a copied user command.

This target is the right default for ARMv8-M porting, Cortex-M33 behavior,
MPU/FPU/BASEPRI paths, loadable app execution, XIP ELF execution, and the
`qemu-armv8m` GitHub Actions `kernel_tc` matrix.

## Decision Guide

Use `qemu-armv8m` when:

- the change touches ARMv8-M, Cortex-M33, MPU, FPU, or nested interrupt paths;
- the change needs protected/loadable/XIP app-package validation;
- the change is part of the `qemu-armv8m` port or its CI matrix;
- you need `kernel_tc` coverage for `hello`, `loadable_all`, `loadable_apps`,
  and `xip_all`.

Use `qemu` when:

- the change needs to preserve existing LM3S6965 or ARMv7-M behavior;
- the change depends on the old `tc_64k`, `tc_1m`, `tc_16m`, `tc_16m_grpc`,
  `build_test`, or `smartfs` configurations;
- the change affects existing QEMU filesystem, network, or grpc coverage;
- a regression was originally reported on the legacy `qemu` target.

If a change is architecture-independent and affects shared kernel, driver,
filesystem, or testcase behavior, run the target that matches the regression
surface. Run both target families only when the change can plausibly affect
both ARMv7-M and ARMv8-M behavior.
