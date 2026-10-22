# NuttX allocator TASH verification

Enable `CONFIG_EXAMPLES_NUTTX_MM_TEST=y` in a **flat, pthread-enabled,
multiheap** build using `CONFIG_MM_NUTTX=y`. The initial target is
`qemu-armv8m/hello`. The existing Make/Kconfig application discovery finds the
local `Kconfig` and `Make.defs`; no CMake integration is required.

Run from TASH:

```text
help
nuttx_mm_test
ps
```

Expected completion:

```text
NXMM BEGIN backend=12.13.0 revision=b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35 ...
NXMM CASE public PASS ...
NXMM CASE failure-preserves-data PASS ...
NXMM CASE multiheap PASS ...
NXMM CASE fragmentation PASS ...
NXMM CASE threaded PASS ...
NXMM RESULT PASS passed=5 failed=0
```

The begin line verifies the linked backend identity. The tests exercise real
public `malloc/calloc/realloc/memalign` and `*_at` wrappers. The fixed-seed
fragmentation case retains 24 blocks, checks all live byte patterns before each
layout change, and performs 1,024 steps. Four pthreads run independent workloads
after a shared start gate. Every created worker must join.

QEMU's `MM_ASSERT_ON_FAIL` product policy stays enabled. Expected failure checks
therefore call the `mm_*` backend-facing API directly on the heap owning a real
public allocation. They verify calloc multiplication overflow and realloc
failure with an arena-sized request while retaining the original data. They do
not request SIZE_MAX from realloc, since NuttX debug builds deliberately panic
on size-alignment overflow.

The failure case also simulates the UP fault diagnostic context by temporarily
setting `abort_mode` with IRQs masked. A held metadata lock must return
`-EAGAIN`; an idle, valid heap must return success. Both calls must leave the
heap lock and task cancellation state unchanged. The test restores the prior
abort/IRQ state before releasing locks or checking results. It does not trigger
a panic and is **not validation of an actual crash dump**.

The test does not validate SMP, interrupt-context deferred free, app
unload/reload, MPU isolation or all optional allocators. Run the existing
`kernel_tc` suite separately for broader regressions. Keep QEMU results distinct
from physical-board results.

A fresh-run helper is provided for the flat `hello` ELF:

```bash
python3 apps/examples/nuttx_mm_test/run_qemu.py \
  --output build/nuttx-mm-validation/hello \
  --timeout 180
```

It requires a new/empty output directory, records the ELF/config SHA-256 and Git
state, waits for fresh TASH command output, rejects target faults, verifies all
five cases and checks that the test task exits. It saves `serial.log` and
`result.json`; these generated artifacts are not source files.
