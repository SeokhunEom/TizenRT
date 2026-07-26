#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# ─── How to run ───
# uv run test_os_api_test_provider_guards.py

from __future__ import annotations

import re
import unittest
from pathlib import Path
from typing import Final

ROOT: Final = Path(__file__).resolve().parents[3]
PROVIDER: Final = ROOT / "os/drivers/os_api_test/kernel/test_task.c"
TASK_WRAPPER: Final = ROOT / "apps/examples/testcase/le_tc/kernel/tc_task.c"
HEAP_WRAPPER: Final = ROOT / "apps/examples/testcase/le_tc/kernel/tc_umm_heap.c"
LIFECYCLE_DISPATCH: Final = re.compile(
    r"#if defined\(CONFIG_SCHED_STARTHOOK\) && defined\(CONFIG_BUILD_PROTECTED\)\s+"
    r"case TESTIOC_TASK_LIFECYCLE_TEST:\s+"
    r"ret = test_task_lifecycle\(arg\);\s+"
    r"break;\s+"
    r"#endif"
)
LIFECYCLE_WRAPPER: Final = re.compile(
    r"#if defined\(CONFIG_SCHED_STARTHOOK\) && "
    r"defined\(CONFIG_BUILD_PROTECTED\) && "
    r"defined\(CONFIG_DRIVERS_OS_API_TEST\)\s+"
    r"static void tc_task_lifecycle_test\(void\)"
)
KMM_WRAPPER: Final = re.compile(
    r"#if defined\(CONFIG_DRIVERS_OS_API_TEST\) && "
    r"defined\(CONFIG_MM_KERNEL_HEAP\)\s+"
    r"enum tc_kmm_heap_case_e"
)


class ProviderGuardContractTest(unittest.TestCase):
    def test_lifecycle_dispatch_is_guarded_when_command_is_not_declared(self) -> None:
        provider = PROVIDER.read_text(encoding="utf-8")
        self.assertIsNotNone(
            LIFECYCLE_DISPATCH.search(provider),
            "task lifecycle dispatch must use the command declaration guard",
        )

    def test_lifecycle_wrapper_uses_the_public_command_guard(self) -> None:
        wrapper = TASK_WRAPPER.read_text(encoding="utf-8")
        self.assertIsNotNone(
            LIFECYCLE_WRAPPER.search(wrapper),
            "task lifecycle wrapper must not reference a hidden command",
        )

    def test_kmm_wrapper_uses_the_public_command_guard(self) -> None:
        wrapper = HEAP_WRAPPER.read_text(encoding="utf-8")
        self.assertIsNotNone(
            KMM_WRAPPER.search(wrapper),
            "KMM wrapper must not reference a hidden command",
        )


if __name__ == "__main__":
    unittest.main()
