# QEMU ARMv8-M MPS2-AN505

This target boots TizenRT on QEMU's `mps2-an505` Cortex-M33 machine.

For the role split between this ARMv8-M target and the legacy LM3S6965 QEMU
target, see [QEMU Target Roles](../qemu-targets.md).

The QEMU ARMv8-M target supports the following configurations:

| Configuration | Build model | Runtime image layout |
| --- | --- | --- |
| `hello` | Flat build with TASH and `kernel_tc` built into the kernel image | Kernel image only |
| `loadable_all` | Protected build with a loadable ELF app package | Kernel image plus `app1` package |
| `loadable_apps` | XIP kernel with a loadable ELF app package | Kernel image plus `app1` package |
| `xip_all` | XIP kernel and XIP ELF app/common packages | Kernel image plus `common` and `app1` packages |

## What This Port Adds

The `hello`, `loadable_all`, `loadable_apps`, and `xip_all` configurations are
kept close to the corresponding `bk7239n` application layouts while using the
QEMU MPS2-AN505 memory map.

The loadable configurations use QEMU's generic loader device to place generated
application packages in the emulated SSRAM window. Board initialization then
copies those packages into tmpfs and starts them through the existing binfmt
loader path.

For non-XIP loadable app configurations:

- `app1` is loaded at `0x10300000`.

For `xip_all`:

- `common` is loaded at `0x102c0000`.
- `app1` is loaded at `0x10360000`.

The `kernel_tc` command is registered from the app package for loadable
configurations and from the kernel image for flat configurations.

## Prerequisites

Install:

- QEMU with `qemu-system-arm` and the `mps2-an505` machine.
- An ARM embedded GCC toolchain that provides `arm-none-eabi-gcc`,
  `arm-none-eabi-nm`, `arm-none-eabi-readelf`, and related binutils.

When using a native toolchain, make sure it is on `PATH` before building:

```sh
export PATH=/path/to/arm-none-eabi/bin:$PATH
```

## Build

From the repository root:

```sh
cd os
make distclean
./tools/configure.sh qemu-armv8m/hello
make
```

Replace `hello` with any supported configuration:

```sh
./tools/configure.sh qemu-armv8m/loadable_all
./tools/configure.sh qemu-armv8m/loadable_apps
./tools/configure.sh qemu-armv8m/xip_all
```

Run `make distclean` before switching configurations because
`configure.sh` refuses to overwrite an existing configuration.

The primary output is:

```text
../build/output/bin/tinyara
```

Loadable configurations also generate:

```text
../build/output/bin/app1
```

`xip_all` additionally generates:

```text
../build/output/bin/common
```

## Run `hello`

`hello` is a flat image, so run only the kernel image:

```sh
qemu-system-arm \
  -M mps2-an505 \
  -kernel ../build/output/bin/tinyara \
  -nographic
```

At the TASH prompt:

```text
kernel_tc
```

## Run `loadable_all` or `loadable_apps`

Build either `qemu-armv8m/loadable_all` or `qemu-armv8m/loadable_apps`, then
load the generated `app1` package with QEMU:

```sh
qemu-system-arm \
  -M mps2-an505 \
  -kernel ../build/output/bin/tinyara \
  -device loader,file=../build/output/bin/app1,addr=0x10300000,force-raw=on \
  -nographic
```

At the TASH prompt:

```text
kernel_tc
```

## Run `xip_all`

Build `qemu-armv8m/xip_all`, then load both generated packages:

```sh
qemu-system-arm \
  -M mps2-an505 \
  -kernel ../build/output/bin/tinyara \
  -device loader,file=../build/output/bin/common,addr=0x102c0000,force-raw=on \
  -device loader,file=../build/output/bin/app1,addr=0x10360000,force-raw=on \
  -nographic
```

At the TASH prompt:

```text
kernel_tc
```

## Console Troubleshooting

The QEMU commands in this document are intended to be run from the `os`
directory. From the repository root, use `build/output/bin/tinyara` instead of
`../build/output/bin/tinyara`.

If the console repeatedly prints the same `System Information` block or repeats
the last TASH command, first check whether QEMU is running with a closed or
non-interactive stdin. For example, piping input directly into
`-display none -serial stdio -monitor none` can make the QEMU stdio character
device feed repeated NUL bytes to TASH after stdin reaches EOF. TASH currently
does not drop NUL bytes in its input loop, so the serial log can look like the
same command is being executed over and over.

For an interactive local run, prefer the board `download` target or the
`-nographic` command shown above:

```sh
make download
```

For scripted or detached runs, keep the serial backend open instead of piping a
finite stdin stream into `-serial stdio`. One option is to use a PTY:

```sh
qemu-system-arm \
  -M mps2-an505 \
  -kernel ../build/output/bin/tinyara \
  -display none \
  -serial pty \
  -monitor none
```

QEMU prints a line similar to:

```text
char device redirected to /dev/pts/7 (label serial0)
```

Open another Linux terminal and connect to that PTY path. `picocom` is the
cleanest option when it is installed:

```sh
picocom -b 115200 /dev/pts/7
```

Exit `picocom` with `Ctrl-A`, then `Ctrl-X`.

If `picocom` is not available, `screen` can also be used:

```sh
screen /dev/pts/7 115200
```

Exit `screen` with `Ctrl-A`, then `K`, then `y`.

For a minimal shell-only check, read the PTY in one terminal and write commands
to it from another:

```sh
cat /dev/pts/7
```

```sh
printf 'ps\n' > /dev/pts/7
```

Replace `/dev/pts/7` with the actual path printed by QEMU.

## Expected `kernel_tc` Results

The verified results for the supported configurations are:

```text
qemu-armv8m/hello         Kernel TC End [PASS : 449, FAIL : 0]
qemu-armv8m/loadable_all  Kernel TC End [PASS : 447, FAIL : 0]
qemu-armv8m/loadable_apps Kernel TC End [PASS : 447, FAIL : 0]
qemu-armv8m/xip_all       Kernel TC End [PASS : 447, FAIL : 0]
```

The pass count differs between the flat and protected/loadable configurations
because some tests are conditional on the build model and enabled features.

## GitHub Actions CI

The `qemu-armv8m` GitHub Actions workflow builds every supported defconfig and
runs `kernel_tc` on QEMU:

```text
hello, loadable_all, loadable_apps, xip_all
```

The workflow uses the `tizenrt/tizenrt:1.5.8` build image, installs
`qemu-system-arm` on the runner, and uploads each QEMU serial log as an
artifact. After a local build, the same QEMU runner can be used from the
repository root:

```sh
python3 .github/scripts/qemu-armv8m-kernel-tc.py --config hello --timeout 600
```

## Notes

- `-display none -serial stdio -monitor none` is useful for automation only
  when the controlling process keeps stdin open and consumes the serial stream
  correctly. For manual use, prefer `-nographic`.
- The load addresses above are part of the QEMU board contract. If the linker
  scripts or package layout change, update both the QEMU command and
  `os/board/qemu-armv8m/src/qemu_armv8m_boot.c`.
- The timer TC validates the POSIX `timer_gettime()` remaining-time semantics
  with one scheduler tick of tolerance, because the returned remaining time is
  tick-granular on this target.
