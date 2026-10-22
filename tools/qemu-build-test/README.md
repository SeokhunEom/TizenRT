# QEMU kernel and full-set validation

The first CI milestone, `98de953981d42fab089f3b6ca90b0162ab3340f6`, is a
standalone ARMv7-M / Cortex-M3 kernel build. Stop at that commit when only the
kernel work and its CI are needed. Subsequent commits add isolated RAM storage,
filesystem fixes, C++ coverage, networking and the second CI job.

`qemu/kernel_test` runs the existing kernel suite without C++, networking,
SmartFS fixtures or filesystem/driver suites. Procfs and tmpfs remain available
to the kernel and libc tests. `qemu/build_test` enables the full workload. Both
configurations use patched QEMU 2.12.0 with 16 MiB SRAM and GNU Arm Embedded
GCC 10.3-2021.10 on native ARM64 Docker.

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
for checker in tools/qemu-build-test/test-*.py; do
  python3 -B "$checker" -v || exit
done
evidence=tmp/qemu-kernel-test-ci
bash tools/qemu-build-test/ci-build.sh "$evidence" qemu/kernel_test
python3 tools/qemu-build-test/kernel-test.py --root . \
  --image "$(cat "$evidence/image-id.txt")" --rounds 3 \
  --output "$evidence/runtime"
python3 tools/qemu-build-test/ci-summary.py --output "$evidence"
```

To run the full configuration, use a separate evidence directory:

```sh
evidence=tmp/qemu-full-set-ci
bash tools/qemu-build-test/ci-build.sh "$evidence" qemu/build_test
python3 tools/qemu-build-test/full-set.py --root . \
  --image "$(cat "$evidence/image-id.txt")" --rounds 3 \
  --output "$evidence/runtime"
python3 tools/qemu-build-test/ci-summary.py --output "$evidence"
```

## Required results

Each job runs three rounds on one boot. Every suite checks the individual
PASS/FAIL lines against its completion summary.

| Suite | PASS | FAIL | Configuration |
| --- | ---: | ---: | --- |
| Kernel | 432 | 0 | Both |
| Network | 163 | 0 | Full |
| libc++ | 795 | 0 | Full |
| Filesystem | 203 | 0 | Full |
| Drivers | 14 | 8 | Full |

The full run also checks TCP/UDP loopback and bidirectional host peers, C++
constructors, RTTI, exceptions and threads, plus SmartFS seek/write, circular
logging and 100 fill/delete loops per round. QEMU's user network and the host
peer live inside a Docker container with external networking disabled.

The eight driver failures are missing PWM, watchdog and ADC devices. The
checker retains the raw failures and accepts only the exact signatures in
`known-driver-failures.json`, while confirming those device nodes are absent.
The full result is therefore `pass_with_known_driver_failures`, not zero FAIL.

Both runners require task cleanup and stable heap usage between rounds two and
three. The full runner additionally checks a storage sentinel, mount inventory,
file inventory and filesystem capacity. Fewer than three rounds or
`full-set.py --omit-kernel` are diagnostics and cannot satisfy CI. Faults,
incomplete output, timeouts, unexpected QEMU exit, remaining tasks and growing
heap usage fail the run.

`boot-smoke.py`, `cpp-smoke.py`, `storage-smoke.py`, `network-smoke.py` and
`run-testcases.py` are optional diagnostics for the full configuration. They do
not replace either CI job. `network-smoke.py --corrupt-reply` deliberately
injects a bad echo and must fail.

## Evidence and limits

The summary requires identical source commits, ELF hashes and effective/config
hashes between build and execution. CI uploads firmware, configuration, raw
serial output, JSON results and Markdown summary on success or failure, with
seven-day retention. Downloaded GCC and QEMU archives are SHA-256 checked; the
Ubuntu base and Actions are pinned. Apt packages are not bit-for-bit pinned.

This validates the QEMU flat configuration, not physical boards, SMP or protected
builds. Three repeated rounds bound the observed resource checks; they do not
prove the absence of every possible leak.
