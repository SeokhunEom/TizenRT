#!/usr/bin/env python3

from __future__ import annotations

import os
import re
import unittest
from pathlib import Path
from typing import Final


ROOT: Final = Path(os.environ.get("OS_API_TEST_ROOT", Path(__file__).resolve().parents[3]))
WRAPPER_DIR: Final = ROOT / "apps/examples/testcase/le_tc/kernel"
PROVIDER_DIR: Final = ROOT / "os/drivers/os_api_test/kernel"
KCONFIG: Final = WRAPPER_DIR / "Kconfig"
WRAPPER_MAKE: Final = WRAPPER_DIR / "Make.defs"
PROVIDER_MAKE: Final = PROVIDER_DIR / "Make.defs"
SELECTED_WRAPPERS: Final = {
    "TC_KERNEL_NET_PBUF": "tc_kernel_net_pbuf.c",
    "TC_KERNEL_PIPE": "tc_pipe.c",
    "TC_KERNEL_PM": "tc_pm.c",
    "TC_KERNEL_PROCFS": "tc_procfs.c",
    "TC_KERNEL_RTC": "tc_rtc.c",
    "TC_KERNEL_VFS": "tc_vfs.c",
    "TC_KERNEL_WDOG": "tc_wdog.c",
    "TC_KERNEL_MEM_LEAK_CHECKER": "tc_mem_leak_checker.c",
    "TC_KERNEL_REBOOT_REASON": "tc_reboot_reason.c",
}


def selected_symbols(kconfig: str) -> set[str]:
    return set(re.findall(r"^\s*select\s+(TC_KERNEL_[A-Z0-9_]+)\b", kconfig, re.MULTILINE))


def referenced_sources(makefile: str, prefix: str) -> set[str]:
    return set(re.findall(rf"\bCSRCS\s*\+=\s*({prefix}[A-Za-z0-9_]+\.c)\b", makefile))


class AtomicTopologyTest(unittest.TestCase):
    def test_makefiles_reference_only_sources_owned_by_this_tree(self) -> None:
        groups = (
            (WRAPPER_DIR, referenced_sources(WRAPPER_MAKE.read_text(encoding="utf-8"), "tc_")),
            (PROVIDER_DIR, referenced_sources(PROVIDER_MAKE.read_text(encoding="utf-8"), "test_")),
        )
        missing = sorted(str(directory / source) for directory, sources in groups for source in sources if not (directory / source).is_file())
        self.assertEqual([], missing, f"Make.defs references missing sources: {missing}")

    def test_selected_capabilities_have_an_owned_wrapper(self) -> None:
        selected = selected_symbols(KCONFIG.read_text(encoding="utf-8"))
        missing = sorted(source for symbol, source in SELECTED_WRAPPERS.items() if symbol in selected and not (WRAPPER_DIR / source).is_file())
        self.assertEqual([], missing, f"Kconfig selects wrappers absent from this tree: {missing}")

    def test_missing_source_mutation_is_rejected(self) -> None:
        sources = referenced_sources("CSRCS += tc_future_provider.c\n", "tc_")
        missing = [source for source in sources if not (WRAPPER_DIR / source).is_file()]
        self.assertEqual(["tc_future_provider.c"], missing)


if __name__ == "__main__":
    unittest.main()
