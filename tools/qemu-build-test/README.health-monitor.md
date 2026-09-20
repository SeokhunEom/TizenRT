# Health Monitor runners

Use the [QEMU validation instructions](../../os/kernel/health_monitor/tests/qemu/README.md)
to prepare the pinned QEMU tree and build each test profile before running these
scripts. This branch's older QEMU defconfig alone does not provide the required
storage and device-registration fixtures.

`qemu_common.py` is copied unchanged from QEMU commit
`ceef1353723842ed051379d6f370319d4f00dfaf` as the runners' shared dependency.
The archived overlay installs the two Health Monitor runners into that QEMU
tree, where the same common module and the full regression suite already exist.
