from __future__ import annotations

from pathlib import Path
from typing import Final


Task15Descriptor = tuple[str, int, str, str, str, str]

TASK15_REGRESSIONS: Final[frozenset[Task15Descriptor]] = frozenset({
    ("TESTIOC_WDOG_TEST", 35, "test_wdog", "wdog_main", "CONFIG_TC_KERNEL_WDOG", "defined(CONFIG_TC_KERNEL_WDOG) && defined(CONFIG_WATCHDOG)"),
    ("TESTIOC_LOG_DUMP_TEST", 42, "test_log_dump", "log_dump_main", "CONFIG_TC_KERNEL_LOG_DUMP", "defined(CONFIG_TC_KERNEL_LOG_DUMP) && defined(CONFIG_LOG_DUMP)"),
    ("TESTIOC_BINARY_MANAGER_TEST", 43, "test_binary_manager", "binary_manager_main", "CONFIG_TC_KERNEL_BINARY_MANAGER", "defined(CONFIG_TC_KERNEL_BINARY_MANAGER) && defined(CONFIG_BINARY_MANAGER)"),
    ("TESTIOC_MEM_LEAK_CHECKER_TEST", 44, "test_mem_leak_checker", "mem_leak_checker_main", "CONFIG_TC_KERNEL_MEM_LEAK_CHECKER", "defined(CONFIG_TC_KERNEL_MEM_LEAK_CHECKER) && defined(CONFIG_MEM_LEAK_CHECKER)"),
    ("TESTIOC_REBOOT_REASON_TEST", 45, "test_reboot_reason", "reboot_reason_main", "CONFIG_TC_KERNEL_REBOOT_REASON", "defined(CONFIG_TC_KERNEL_REBOOT_REASON) && defined(CONFIG_SYSTEM_REBOOT_REASON)"),
    ("TESTIOC_PM_TEST", 46, "test_pm", "pm_tc_main", "CONFIG_TC_KERNEL_PM", "defined(CONFIG_TC_KERNEL_PM) && defined(CONFIG_PM)"),
    ("TESTIOC_RTC_TEST", 48, "test_rtc", "rtc_main", "CONFIG_TC_KERNEL_RTC", "defined(CONFIG_TC_KERNEL_RTC) && defined(CONFIG_RTC_DRIVER)"),
    ("TESTIOC_BINFMT_TEST", 50, "test_binfmt", "binfmt_main", "CONFIG_TC_KERNEL_BINFMT", "defined(CONFIG_TC_KERNEL_BINFMT) && defined(CONFIG_BINFMT_ENABLE)"),
})


def direct_task15_dispatch_symbols(root: Path) -> list[str]:
    driver = (root / "os/drivers/os_api_test/os_api_test_drv.c").read_text(encoding="utf-8")
    return sorted(symbol for symbol, *_rest in TASK15_REGRESSIONS if f"case {symbol}:" in driver)
