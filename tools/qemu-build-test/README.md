# QEMU kernel validation

The first CI milestone is a standalone ARMv7-M / Cortex-M3 kernel build.
`qemu/kernel_test` runs the existing kernel suite without C++, networking,
SmartFS fixtures or filesystem/driver suites. Procfs and tmpfs remain available
to the kernel and libc tests. The fixture uses patched QEMU 2.12.0 with 16 MiB
SRAM and GNU Arm Embedded GCC 10.3-2021.10 on native ARM64 Docker.

`CONFIG_MAX_WDOGPARMS=4` accommodates the upstream three-argument condition
timeout callback. There is no change to `pthread_condtimedwait.c`. Robust mutex
ownership still needs its separate fix: tasks and pthreads share the ownership
list, and task recovery cancels semaphore waits before releasing owned mutexes.
The common TCB layout change requires a clean rebuild.

## Run locally

Run from the repository root on native ARM64 Docker (including Apple Silicon
Docker Desktop). Use a new output directory and do not build two configurations
in the same checkout concurrently.

```sh
python3 tools/qemu-build-test/test-kernel-test.py -v
python3 tools/qemu-build-test/test-ci-summary.py -v
python3 tools/qemu-build-test/test-ci-build.py -v
evidence=tmp/qemu-kernel-test-ci
bash tools/qemu-build-test/ci-build.sh "$evidence" qemu/kernel_test
python3 tools/qemu-build-test/kernel-test.py --root . \
  --image "$(cat "$evidence/image-id.txt")" --rounds 3 \
  --output "$evidence/runtime"
python3 tools/qemu-build-test/ci-summary.py --output "$evidence"
```

The runner checks all 432 PASS lines, zero failures, suite completion, task
cleanup and stable heap usage between rounds two and three. Fewer than three
rounds are diagnostics and cannot satisfy CI. Faults, incomplete output,
timeouts, new tasks and growing heap usage fail the run.

The summary requires identical source commits, ELF hashes and effective/config
hashes between build and execution. CI uploads firmware, configuration, raw
serial output, JSON results and Markdown summary on success or failure, with
seven-day retention. Downloaded GCC and QEMU archives are SHA-256 checked; the
Ubuntu base and Actions are pinned. Apt packages are not bit-for-bit pinned.

This validates the QEMU flat configuration, not physical boards, SMP or protected
builds. Three repeated rounds bound the observed resource checks; they do not
prove the absence of every possible leak.
