#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///

# ─── How to run ───
# python3 -m unittest discover -s .github/scripts/tests -p test_os_api_test_optionals.py -v

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Final

from os_api_test_task15_registry_contract import TASK15_REGRESSIONS, direct_task15_dispatch_symbols
from test_os_api_test_registry import analyze_registry


ROOT: Final = Path(__file__).resolve().parents[3]
OPTIONAL_FEATURES: Final = frozenset({
    "CONFIG_DRIVERS_OS_API_TEST",
    "CONFIG_EXAMPLES_TESTCASE_KERNEL",
    "CONFIG_TC_KERNEL_BINFMT",
    "CONFIG_BINFMT_ENABLE",
    "CONFIG_TC_KERNEL_BINARY_MANAGER",
    "CONFIG_BINARY_MANAGER",
    "CONFIG_TC_KERNEL_LOG_DUMP",
    "CONFIG_LOG_DUMP",
    "CONFIG_TC_KERNEL_MEM_LEAK_CHECKER",
    "CONFIG_MEM_LEAK_CHECKER",
    "CONFIG_TC_KERNEL_REBOOT_REASON",
    "CONFIG_SYSTEM_REBOOT_REASON",
    "CONFIG_TC_KERNEL_PM",
    "CONFIG_PM",
    "CONFIG_TC_KERNEL_RTC",
    "CONFIG_RTC_DRIVER",
    "CONFIG_TC_KERNEL_WDOG",
    "CONFIG_WATCHDOG",
})
PROVIDER_SOURCES: Final = frozenset({"test_binfmt.c", "test_binary_manager.c", "test_log_dump.c", "test_mem_leak_checker.c", "test_reboot_reason.c", "test_pm.c", "test_rtc.c", "test_wdog.c"})
WRAPPER_SOURCES: Final = frozenset({"tc_binfmt.c", "tc_binary_manager.c", "tc_log_dump.c", "tc_mem_leak_checker.c", "tc_reboot_reason.c", "tc_pm.c", "tc_rtc.c", "tc_wdog.c"})
QEMU_BINFMT_CONFIGS: Final = frozenset({"loadable_all", "loadable_apps", "xip_all"})
KCONFIG_DEPENDENCIES: Final = {
    "TC_KERNEL_BINFMT": "depends on BINFMT_ENABLE",
    "TC_KERNEL_BINARY_MANAGER": "depends on BINARY_MANAGER",
    "TC_KERNEL_LOG_DUMP": "depends on LOG_DUMP",
    "TC_KERNEL_MEM_LEAK_CHECKER": "depends on MEM_LEAK_CHECKER",
    "TC_KERNEL_REBOOT_REASON": "depends on SYSTEM_REBOOT_REASON",
    "TC_KERNEL_PM": "depends on PM",
    "TC_KERNEL_RTC": "depends on RTC_DRIVER",
    "TC_KERNEL_WDOG": "depends on WATCHDOG",
}


def enabled_symbols(path: Path) -> frozenset[str]:
    return frozenset(
        line.partition("=")[0]
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.endswith("=y")
    )


def selected_sources(makefile: Path, config: frozenset[str]) -> subprocess.CompletedProcess[str]:
    assignments = "\n".join(f"{symbol}=y" for symbol in sorted(config))
    with tempfile.TemporaryDirectory(prefix="task15-make-") as directory:
        probe = Path(directory) / "Makefile"
        probe.write_text(f"{assignments}\ninclude {makefile}\nall:\n\t@printf '%s\\n' \"$(CSRCS)\"\n", encoding="utf-8")
        return subprocess.run(["make", "--no-print-directory", "-f", str(probe), "all"], capture_output=True, text=True, timeout=10, check=False)


def registry_translation_unit(config: frozenset[str]) -> tuple[subprocess.CompletedProcess[str], subprocess.CompletedProcess[str]]:
    definitions = "\n".join(f"#define {symbol} 1" for symbol in sorted(config))
    source = (
        f"{definitions}\n"
        "#define OS_API_TEST_KERNEL_DESCRIPTOR(symbol, id, provider, provider_source, wrapper, wrapper_source, test_gate) int symbol = id;\n"
        f"#include \"{ROOT / 'os/drivers/os_api_test/os_api_test_kernel_registry.inc'}\"\n"
        "int main(void) { return 0; }\n"
    )
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-x", "c", "-"]
    compiled = subprocess.run([*command, "-fsyntax-only"], input=source, capture_output=True, text=True, timeout=10, check=False)
    preprocessed = subprocess.run([*command, "-E", "-P"], input=source, capture_output=True, text=True, timeout=10, check=False)
    return compiled, preprocessed


def compile_header(config: frozenset[str], source: str) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="task15-header-") as directory:
        root = Path(directory)
        include = root / "include" / "tinyara"
        (include / "fs").mkdir(parents=True)
        (include / "config.h").write_text("", encoding="utf-8")
        (include / "fs" / "ioctl.h").write_text("#define _TESTIOC(id) (id)\n", encoding="utf-8")
        probe = root / "header_probe.c"
        probe.write_text(source, encoding="utf-8")
        return subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", *(f"-D{symbol}" for symbol in sorted(config)), f"-I{root / 'include'}", f"-I{ROOT / 'os' / 'include'}", "-fsyntax-only", str(probe)],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )


class OptionalProviderRegistryTest(unittest.TestCase):
    def test_optional_descriptors_are_atomic_when_all_capabilities_are_enabled(self) -> None:
        report = analyze_registry(ROOT)
        actual = {
            (descriptor["symbol"], descriptor["command_id"], descriptor["provider"], descriptor["wrapper"], descriptor["test_gate"], descriptor["predicate"])
            for descriptor in report["descriptors"]
        }
        self.assertTrue(TASK15_REGRESSIONS.issubset(actual), sorted(TASK15_REGRESSIONS - actual))

    def test_optional_legacy_adapters_are_removed_when_registry_rows_exist(self) -> None:
        header = (ROOT / "os/include/tinyara/os_api_test_drv.h").read_text(encoding="utf-8")
        proto = (ROOT / "os/drivers/os_api_test/os_api_test_proto.h").read_text(encoding="utf-8")
        kernel_main = (ROOT / "apps/examples/testcase/le_tc/kernel/kernel_tc_main.c").read_text(encoding="utf-8")
        self.assertEqual([], direct_task15_dispatch_symbols(ROOT))
        for symbol, _identifier, provider, wrapper, _gate, _predicate in TASK15_REGRESSIONS:
            with self.subTest(symbol=symbol):
                self.assertNotIn(f"#define {symbol}", header)
                self.assertNotIn(f"int {provider}(int cmd, unsigned long arg);", proto)
                self.assertNotIn(f"\t{wrapper}();", kernel_main)

    def test_optional_provider_and_wrapper_sources_follow_exact_feature_gates(self) -> None:
        for makefile, expected in ((ROOT / "os/drivers/os_api_test/kernel/Make.defs", PROVIDER_SOURCES), (ROOT / "apps/examples/testcase/le_tc/kernel/Make.defs", WRAPPER_SOURCES)):
            with self.subTest(makefile=makefile, matrix="enabled"):
                result = selected_sources(makefile, OPTIONAL_FEATURES)
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertTrue(expected.issubset(frozenset(result.stdout.split())), result.stdout)

        disabled = enabled_symbols(ROOT / ".github/scripts/tests/fixtures/os_api_test/no-pm-no-binary-manager.config")
        for makefile, forbidden in ((ROOT / "os/drivers/os_api_test/kernel/Make.defs", {"test_pm.c", "test_binary_manager.c"}), (ROOT / "apps/examples/testcase/le_tc/kernel/Make.defs", {"tc_pm.c", "tc_binary_manager.c"})):
            with self.subTest(makefile=makefile, matrix="no-pm-no-binary-manager"):
                result = selected_sources(makefile, disabled)
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertFalse(forbidden & frozenset(result.stdout.split()), result.stdout)

    def test_binfmt_is_enabled_only_for_qemu_loadable_defconfigs(self) -> None:
        actual = frozenset(
            defconfig.parent.name
            for defconfig in (ROOT / "build/configs/qemu-armv8m").glob("*/defconfig")
            if "CONFIG_BINFMT_ENABLE=y" in defconfig.read_text(encoding="utf-8")
        )
        self.assertEqual(QEMU_BINFMT_CONFIGS, actual)

    def test_optional_kconfig_gates_keep_matching_capability_dependencies(self) -> None:
        kconfig = (ROOT / "apps/examples/testcase/le_tc/kernel/Kconfig").read_text(encoding="utf-8")
        for gate, dependency in KCONFIG_DEPENDENCIES.items():
            with self.subTest(gate=gate):
                start = kconfig.index(f"config {gate}\n")
                end = kconfig.find("\nconfig ", start + 1)
                self.assertIn(dependency, kconfig[start:] if end < 0 else kconfig[start:end])

    def test_optional_registry_and_header_contracts_follow_disabled_capabilities(self) -> None:
        compiled, preprocessed = registry_translation_unit(OPTIONAL_FEATURES)
        self.assertEqual(0, compiled.returncode, compiled.stderr)
        self.assertEqual(0, preprocessed.returncode, preprocessed.stderr)
        for symbol, _identifier, _provider, _wrapper, _gate, _predicate in TASK15_REGRESSIONS:
            with self.subTest(symbol=symbol, matrix="enabled"):
                self.assertIn(symbol, preprocessed.stdout)

        disabled = enabled_symbols(ROOT / ".github/scripts/tests/fixtures/os_api_test/no-pm-no-binary-manager.config")
        compiled, preprocessed = registry_translation_unit(disabled)
        self.assertEqual(0, compiled.returncode, compiled.stderr)
        self.assertEqual(0, preprocessed.returncode, preprocessed.stderr)
        for symbol in ("TESTIOC_PM_TEST", "TESTIOC_BINARY_MANAGER_TEST"):
            with self.subTest(symbol=symbol, matrix="no-pm-no-binary-manager"):
                self.assertNotIn(symbol, preprocessed.stdout)
                result = compile_header(disabled, f"#include <tinyara/os_api_test_drv.h>\nint value = {symbol};\n")
                self.assertNotEqual(0, result.returncode, result.stderr)


if __name__ == "__main__":
    unittest.main()
