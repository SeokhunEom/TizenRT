# All defconfig build CI

`.github/workflows/defconfig-build.yml` builds every `defconfig` under
`build/configs/{rtl8730e,bk7239n,rtl8721csm,qemu}` on pushes, pull requests,
and manual dispatches. New configurations are discovered automatically.
Discovery fails if any board has no configurations or a configuration cannot
be represented, so coverage cannot silently shrink to zero for a board.

The workflow is self-contained: copy only `.github/workflows/defconfig-build.yml`
to another TizenRT branch to discover and attempt its builds. Discovery and Docker
execution are embedded in the YAML; it does not call `tools/defconfig-ci` or run
source-dependent host tests before building. The destination branch still needs
the four board directories and the normal TizenRT build system. Its firmware
builds can fail independently of whether the workflow starts correctly.

Each configuration gets a fresh Ubuntu x86-64 runner checkout and runs the
normal `os/tools/configure.sh BOARD/CONFIG` and `make` in Docker, including
the board's normal linking and packaging steps. The image is
`tizenrt/tizenrt:$CONFIG_DOCKER_VERSION`, falling back to `1.5.8` like
`os/dbuild.sh`. The runner uses the checked-in defconfigs without patching them
at build time; configuration fixes are reviewed as ordinary source changes.

Twenty configurations can build concurrently. The top-level Make invocation stays
serial, like `dbuild.sh`, because its compilation, linking and packaging stages
are not safe to run concurrently. A failed build does not cancel the others.
The stable `All defconfigs passed` check succeeds only if discovery and every
matrix job succeed; use that check for branch protection. Build jobs have a
60-minute timeout. This workflow verifies compilation and packaging, not
hardware behavior or QEMU runtime tests.

Every build uploads its logs, requested/effective configuration, source commit,
Docker image identity and exit code. Complete or partial firmware is archived
as `firmware.tar.gz`. Artifacts request seven days of retention, subject to the
repository's retention policy. Setup failures and cancellations may leave only
partial evidence; neither counts as a successful build.

The scripts in this directory are optional local helpers. To inspect the matrix:
`python3 tools/defconfig-ci/matrix.py`. To reproduce a job in a fresh checkout on
an x86-64 Docker host:

```sh
bash tools/defconfig-ci/build.sh rtl8730e/flat_apps tizenrt/tizenrt:2.0.0
```

For validation of this change, push the feature branch to the personal
`origin` repository and inspect its **All defconfig builds** run. No upstream
push, pull request or workflow dispatch is needed.

## Configuration repairs exposed by the full matrix

- The legacy QEMU `smartfs`, `tc_1m` and `tc_16m_grpc` configurations enabled
  TASH reboot without its `BOARDCTL_RESET` dependency. They now follow Kconfig
  and leave this unsupported command disabled; their test suites remain enabled.
- RTL8721CSM `rdp` and `tls_test` now specify the Wi-Fi test's default five trials
  and its required stress-test library.
- RTL8721CSM `tc` exceeded its 1792 KiB kernel slot. Both kernel slots are now
  2048 KiB; userfs is 3568 KiB, preserving the total 8192 KiB and the final
  boot-parameter location. This test configuration changes flash offsets and
  requires a full flash-layout update before use on a previously flashed board.
- QEMU `tc_16m` is a flat build; stale separate-app packaging entries are removed.
  It uses its configured virtual Wi-Fi device instead of also enabling Ethernet.
- QEMU `tc_64k` keeps its original 256 KiB flash and 64 KiB SRAM limits. Function
  and data sections allow the linker to discard unreachable code, while vector
  and constructor tables are explicitly retained. Test assertion output and
  counter updates use shared functions, preserving diagnostics, cleanup and
  return behavior without duplicating counter updates in every test body.
  Only this smallest configuration enables `TESTCASE_COMPACT_RESULT`: failures
  retain function, source line and API name, omitting operand expressions/values.
  All comparisons and test suites remain enabled.
- QEMU network configurations link the LM3S Ethernet driver using the current
  netmgr interface (the port already present on `codex/qemu-build-test`).
  Ethernet-only builds do not require LWNL event delivery, and poll-disabled
  builds do not reference the unavailable lwIP poll implementation. These are
  build checks; Ethernet traffic still needs separate runtime validation.
- The gRPC TLS adapter uses Mbed TLS 3 output-size and private-key parsing APIs,
  including the context's seeded random generator. QEMU C++ builds treat external
  headers as user headers, avoiding the bare-metal compiler's implicit C linkage
  for gRPC templates. The libc++ `new` declarations explicitly retain C++ linkage
  so `std::nothrow` matches the ABI library, without changing C-header search order.
