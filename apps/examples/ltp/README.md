# LTP POSIX tests for TizenRT

This directory integrates selected Linux Test Project (LTP) 20230516 Open
POSIX tests into TizenRT. The source archive is stored locally, patched during
the build, and compiled into the firmware. The current selection contains 46
scheduler and pthread tests.

## Build for QEMU ARMv8-M

From the repository root:

```sh
cd os
./dbuild.sh qemu-armv8m hello
./dbuild.sh build
```

The `qemu-armv8m/hello` defconfig enables LTP with an 8192-byte child-task
stack. A clean build extracts `ltp-20230516.zip`, applies every patch in
`patches/`, and fails if any patch cannot be applied.

## Run

Run all registered tests in one fresh QEMU session and save both serial and
JSON evidence:

```sh
python3 .github/scripts/qemu-armv8m-ltp.py \
  --log build/qemu-armv8m-ltp/ltp-serial.log \
  --result build/qemu-armv8m-ltp/ltp-result.json
```

Run selected test indices with `--tests`, for example:

```sh
python3 .github/scripts/qemu-armv8m-ltp.py --tests 1 20 46
```

The runner validates the generated command/source manifest against the TASH
registry before booting `qemu-system-arm -M mps2-an505`. It stops on a target
crash or timeout and returns a nonzero host exit status unless every selected
test reports `PASS`.

For manual execution, the generated commands are `ltp_t1` through `ltp_t46`.
The exact command, entry function, and source mapping is generated in
`ltp_manifest.tsv` during the build.

## Integration design

Each selected LTP source contains a `main()` function. The Makefile compiles
it with `-Dmain=<unique_entry>` so all tests can coexist in `libapps.a`.
`ltp_register.sh` then generates:

- TASH `.mdat` and `.pdat` entries;
- `ltp_registry.inc`, which maps each short command to its renamed entry;
- `ltp_manifest.tsv`, used by the host runner for reproducible reporting.

All TASH commands enter `ltp_runner_main`. It launches the selected LTP entry
as a child task, waits for its exit status, and prints one machine-readable
line:

```text
LTP_RESULT ltp_t1 PASS exit=0
```

This wrapper is necessary because an asynchronous TASH callback alone does
not expose the test function's return status to the host.

## Selection and compatibility patches

`LTP_TEST_SUBDIR` in the Makefile defines the selected scheduler and pthread
categories. Content and source-path blacklists exclude tests that require
unsupported TizenRT facilities such as `SCHED_OTHER`, `fork()`, or user
identity APIs. Keep the Makefile and `ltp_register.sh` filters aligned when
changing coverage.

The patches adapt upstream tests to supported TizenRT semantics. In
particular, TizenRT pthread defaults use explicit scheduling at priority 100,
and signal actions are associated with individual TCBs. The compatibility
patches make priority and handler ownership explicit where upstream tests
otherwise assume Linux process behavior.

## Configuration

| Option | Value for QEMU | Purpose |
| --- | ---: | --- |
| `CONFIG_EXAMPLES_LTP` | `y` | Build and register LTP tests |
| `CONFIG_EXAMPLES_LTP_STACKSIZE` | `8192` | LTP child-task stack size |
| `CONFIG_EXAMPLES_LTP_PRIORITY` | `100` | TASH wrapper priority |
| `CONFIG_DISABLE_PTHREAD` | unset | Keep pthread support enabled |
| `CONFIG_BUILTIN_APPS` | `y` | Register TASH commands |
