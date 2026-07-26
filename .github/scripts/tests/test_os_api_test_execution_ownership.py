#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///

# ─── How to run ───
#   uv run .github/scripts/tests/test_os_api_test_execution_ownership.py
#   python3 -m unittest discover -s .github/scripts/tests -p 'test*.py'
# ───────────────────

from __future__ import annotations

import os
import re
import unittest
from dataclasses import dataclass
from pathlib import Path
from typing import Final


ROOT: Final = Path(os.environ.get("OS_API_TEST_ROOT", Path(__file__).resolve().parents[3]))
KERNEL: Final = ROOT / "apps/examples/testcase/le_tc/kernel"
REGISTRY: Final = ROOT / "os/drivers/os_api_test/os_api_test_kernel_registry.inc"
HEADER: Final = ROOT / "os/include/tinyara/os_api_test_drv.h"
ROW_RE: Final = re.compile(
    r"OS_API_TEST_KERNEL_DESCRIPTOR\(TESTIOC_[A-Z0-9_]+, [0-9]+, [a-z0-9_]+, "
    r"[a-z0-9_]+\.c, ([a-z0-9_]+), ([a-z0-9_]+\.c), CONFIG_[A-Z0-9_]+\)"
)


@dataclass(frozen=True, slots=True)
class ExecutionOwner:
    wrapper: str
    leaf: str


OWNERS: Final = {
    owner.wrapper: owner
    for owner in (
        ExecutionOwner("umm_heap_main", "umm_heap_main"),
        ExecutionOwner("task_lifecycle_main", "tc_task_lifecycle_test"),
        ExecutionOwner("sched_foreach_main", "tc_sched_sched_foreach"),
        ExecutionOwner("signal_findaction_null_main", "TESTIOC_SIG_FINDACTION_NULL_TEST"),
        ExecutionOwner("clock_conversion_main", "tc_clock_clock_conversion"),
        ExecutionOwner("mqueue_main", "mqueue_main"),
        ExecutionOwner("timer_deleteall_main", "tc_timer_timer_deleteall"),
        ExecutionOwner("semaphore_kernel_main", "tc_semaphore_kernel"),
        ExecutionOwner("environ_main", "environ_main"),
        ExecutionOwner("errno_main", "errno_main"),
        ExecutionOwner("pthread_kernel_main", "tc_pthread_kernel"),
        ExecutionOwner("irq_kernel_main", "tc_irq_kernel"),
        ExecutionOwner("procfs_main", "procfs_main"),
        ExecutionOwner("pipe_main", "pipe_main"),
        ExecutionOwner("vfs_main", "vfs_main"),
        ExecutionOwner("termios_main", "termios_main"),
        ExecutionOwner("sched_affinity_main", "tc_sched_kernel_affinity"),
        ExecutionOwner("group_signal_main", "tc_group_group_signal"),
        ExecutionOwner("task_starthook_main", "task_starthook_main"),
        ExecutionOwner("sched_state_main", "tc_sched_kernel_state"),
        ExecutionOwner("signal_pendingset_main", "tc_signal_kernel_pendingset"),
        ExecutionOwner("wdog_main", "wdog_main"),
        ExecutionOwner("log_dump_main", "log_dump_main"),
        ExecutionOwner("binary_manager_main", "binary_manager_main"),
        ExecutionOwner("mem_leak_checker_main", "mem_leak_checker_main"),
        ExecutionOwner("reboot_reason_main", "reboot_reason_main"),
        ExecutionOwner("pm_tc_main", "pm_tc_main"),
        ExecutionOwner("rtc_main", "rtc_main"),
        ExecutionOwner("binfmt_main", "binfmt_main"),
    )
}
LEGACY_TOP_LEVEL: Final = frozenset({"environ_main", "errno_main", "mqueue_main", "termios_main", "umm_heap_main"})
DEFERRED_ABI: Final = (
    "TESTIOC_TIMER_CREATE_DELETE_TEST",
    "TESTIOC_TASK_SETCANCELSTATE_TEST",
    "TESTIOC_TASK_SETCANCELTYPE_TEST",
    "TESTIOC_WORK_QUEUE_TEST",
)


def registry_rows() -> dict[str, str]:
    rows: dict[str, str] = {}
    for line in REGISTRY.read_text(encoding="utf-8").splitlines():
        match = ROW_RE.fullmatch(line)
        if match is not None:
            wrapper, source = match.groups()
            rows[wrapper] = source
    return rows


class ExecutionOwnershipTest(unittest.TestCase):
    def test_every_registry_wrapper_has_one_declared_leaf(self) -> None:
        unknown = sorted(set(registry_rows()) - set(OWNERS))
        self.assertEqual([], unknown, f"registry wrappers without an execution owner: {unknown}")

    def test_migrated_internal_leaves_have_one_live_call_site(self) -> None:
        rows = registry_rows()
        duplicated: list[str] = []
        for wrapper, source in rows.items():
            owner = OWNERS[wrapper]
            if owner.leaf == wrapper:
                continue
            text = (KERNEL / source).read_text(encoding="utf-8")
            if owner.leaf.startswith("TESTIOC_"):
                count = len(re.findall(rf"\b{re.escape(owner.leaf)}\b", text))
            else:
                count = len(re.findall(rf"\b{re.escape(owner.leaf)}\s*\(\s*\)\s*;", text))
            if count != 1:
                duplicated.append(f"{wrapper}->{owner.leaf}: {count}")
        self.assertEqual([], duplicated, f"registry leaves must have exactly one live call site: {duplicated}")

    def test_whole_legacy_wrappers_transition_without_gap_or_duplicate(self) -> None:
        rows = registry_rows()
        main = (KERNEL / "kernel_tc_main.c").read_text(encoding="utf-8")
        invalid: list[str] = []
        for wrapper in sorted(LEGACY_TOP_LEVEL):
            direct_count = len(re.findall(rf"^\s*{re.escape(wrapper)}\(\);", main, re.MULTILINE))
            expected = 0 if wrapper in rows else 1
            if direct_count != expected:
                invalid.append(f"{wrapper}: expected {expected}, found {direct_count}")
        self.assertEqual([], invalid, f"legacy-to-registry transition has a gap or duplicate: {invalid}")

    def test_pbuf_surface_arrives_with_its_kernel_owner(self) -> None:
        kconfig = (KERNEL / "Kconfig").read_text(encoding="utf-8")
        main = (KERNEL / "kernel_tc_main.c").read_text(encoding="utf-8")
        header = HEADER.read_text(encoding="utf-8")
        owned = re.search(r"(?m)^config TC_KERNEL_NET_PBUF$", kconfig) is not None
        self.assertEqual(owned, "kernel_net_pbuf_main();" in main)
        self.assertEqual(owned, "struct pbuf_test_args" in header)
        self.assertEqual(owned, "#include <lwip/pbuf.h>" in header)

    def test_legacy_command_abi_arrives_with_core_provider_sources(self) -> None:
        header = HEADER.read_text(encoding="utf-8")
        core_owned = (ROOT / "os/drivers/os_api_test/kernel/test_wqueue.c").is_file()
        for symbol in DEFERRED_ABI:
            with self.subTest(symbol=symbol):
                self.assertEqual(core_owned, symbol in header)


if __name__ == "__main__":
    unittest.main()
