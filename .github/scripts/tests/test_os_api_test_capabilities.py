#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///

# ─── How to run ───
# 1. uv run test_os_api_test_capabilities.py --root /path/to/TizenRT
# 2. python3 -m unittest discover -s .github/scripts/tests -p test_os_api_test_capabilities.py -v

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from dataclasses import asdict
from pathlib import Path
from typing import Final

from os_api_test_pbuf_test_support import EXPECTED, CommandResult, compile_handler, compile_header
from os_api_test_task14_registry_contract import TASK14_REGRESSIONS, direct_task14_dispatch_symbols
from os_api_test_task15_registry_contract import TASK15_REGRESSIONS
from test_os_api_test_registry import analyze_registry

CONFIGS: Final = ("pbuf-legacy", "pbuf-kernel", "pbuf-both")
ROOT: Final = Path(__file__).resolve().parents[3]
TASK14_ROWS: Final = TASK14_REGRESSIONS
TASK14_PROVIDER_SOURCES: Final = frozenset(
    {"test_kmm.c", "test_mqueue.c", "test_environ.c", "test_errno.c", "test_procfs.c", "test_pipe.c", "test_vfs.c", "test_termios.c"}
)
TASK14_WRAPPER_SOURCES: Final = frozenset(
    {"tc_umm_heap.c", "tc_mqueue.c", "tc_environ.c", "tc_errno.c", "tc_procfs.c", "tc_pipe.c", "tc_vfs.c", "tc_termios.c"}
)
TASK14_FEATURES: Final = frozenset(
    {
        "CONFIG_DRIVERS_OS_API_TEST",
        "CONFIG_EXAMPLES_TESTCASE_KERNEL",
        "CONFIG_TC_KERNEL_UMM_HEAP",
        "CONFIG_MM_KERNEL_HEAP",
        "CONFIG_TC_KERNEL_MQUEUE",
        "CONFIG_TC_KERNEL_ENVIRON",
        "CONFIG_TC_KERNEL_ERRNO",
        "CONFIG_TC_KERNEL_PIPE",
        "CONFIG_PIPES",
        "CONFIG_TC_KERNEL_PROCFS",
        "CONFIG_FS_PROCFS",
        "CONFIG_TC_KERNEL_VFS",
        "CONFIG_TC_KERNEL_TERMIOS",
        "CONFIG_SERIAL_TERMIOS",
    }
)


def enabled_symbols(config_path: Path) -> frozenset[str]:
    return frozenset(
        line.partition("=")[0]
        for line in config_path.read_text(encoding="utf-8").splitlines()
        if line.endswith("=y")
    )


def selected_sources(makefile: Path, config: frozenset[str]) -> subprocess.CompletedProcess[str]:
    assignments = "\n".join(f"{symbol}=y" for symbol in sorted(config))
    with tempfile.TemporaryDirectory(prefix="task14-make-") as directory:
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
    base = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-x", "c", "-"]
    compiled = subprocess.run([*base, "-fsyntax-only"], input=source, capture_output=True, text=True, timeout=10, check=False)
    preprocessed = subprocess.run([*base, "-E", "-P"], input=source, capture_output=True, text=True, timeout=10, check=False)
    return compiled, preprocessed


def compile_task14_header(config: frozenset[str], source: str) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="task14-header-") as directory:
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


def kconfig_block(text: str, symbol: str) -> str:
    marker = f"config {symbol}\n"
    start = text.index(marker)
    remainder = text[start + len(marker):]
    end = remainder.find("\nconfig ")
    return remainder if end < 0 else remainder[:end]


class PbufCapabilityContractTest(unittest.TestCase):
    def test_baseline_public_abi_is_legacy_only(self) -> None:
        header = (ROOT / "os/include/tinyara/os_api_test_drv.h").read_text(encoding="utf-8")
        self.assertIn("#if defined(CONFIG_TC_NET_PBUF)\n#include <lwip/pbuf.h>\n#endif", header)
        self.assertNotIn("defined(CONFIG_TC_NET_PBUF) || defined(CONFIG_TC_KERNEL_NET_PBUF)\n#include <lwip/pbuf.h>", header)

    def test_baseline_source_is_selected_for_each_pbuf_capability(self) -> None:
        makefile = (ROOT / "os/drivers/os_api_test/network/Make.defs").read_text(encoding="utf-8")
        self.assertIn("$(CONFIG_TC_NET_PBUF)$(CONFIG_TC_KERNEL_NET_PBUF)", makefile)
        self.assertIn("CSRCS += test_net_pbuf.c", makefile)

    def test_legacy_kconfig_requires_lwip(self) -> None:
        text = (ROOT / "apps/examples/testcase/le_tc/network/Kconfig").read_text(encoding="utf-8")
        self.assertIn("depends on NET_LWIP", kconfig_block(text, "TC_NET_PBUF"))

    def test_commented_private_abi_duplicate_is_absent(self) -> None:
        source = (ROOT / "os/drivers/os_api_test/network/test_net_pbuf.c").read_text(encoding="utf-8")
        self.assertNotIn("// struct pbuf_test_args", source)

    def test_public_header_visibility_matches_legacy_capability(self) -> None:
        for name in CONFIGS:
            with self.subTest(name=name):
                result = compile_header(ROOT, name)
                self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(0, compile_header(ROOT, "pbuf-legacy", reference_abi=True).returncode)
        self.assertNotEqual(0, compile_header(ROOT, "pbuf-kernel", reference_abi=True).returncode)
        self.assertEqual(0, compile_header(ROOT, "pbuf-both", reference_abi=True).returncode)

    def test_legacy_only_ioctl_contract(self) -> None:
        result = compile_handler(ROOT, "pbuf-legacy")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(EXPECTED["pbuf-legacy"], result.stdout)

    def test_kernel_only_ioctl_contract(self) -> None:
        result = compile_handler(ROOT, "pbuf-kernel")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(EXPECTED["pbuf-kernel"], result.stdout)

    def test_kernel_only_rejects_private_legacy_argument(self) -> None:
        result = compile_handler(ROOT, "pbuf-kernel", private_abi=True)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(EXPECTED["pbuf-kernel"], result.stdout)

    def test_both_ioctl_contract(self) -> None:
        result = compile_handler(ROOT, "pbuf-both")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(EXPECTED["pbuf-both"], result.stdout)

    def test_cli_rejects_malformed_arguments(self) -> None:
        result = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "--bogus"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        self.assertEqual(2, result.returncode)


class Task14CapabilityContractTest(unittest.TestCase):
    def test_task14_descriptors_are_registered_with_exact_predicates(self) -> None:
        report = analyze_registry(ROOT)
        actual = {
            (descriptor["symbol"], descriptor["command_id"], descriptor["provider"], descriptor["wrapper"], descriptor["test_gate"], descriptor["predicate"])
            for descriptor in report["descriptors"]
        }
        self.assertEqual(21 + len(TASK15_REGRESSIONS), report["descriptor_count"])
        self.assertTrue(TASK14_ROWS.issubset(actual), sorted(TASK14_ROWS - actual))
        self.assertEqual(direct_task14_dispatch_symbols(ROOT), [])

    def test_task14_legacy_surfaces_use_only_the_registry_adapter(self) -> None:
        header = (ROOT / "os/include/tinyara/os_api_test_drv.h").read_text(encoding="utf-8")
        proto = (ROOT / "os/drivers/os_api_test/os_api_test_proto.h").read_text(encoding="utf-8")
        driver = (ROOT / "os/drivers/os_api_test/os_api_test_drv.c").read_text(encoding="utf-8")
        kernel_main = (ROOT / "apps/examples/testcase/le_tc/kernel/kernel_tc_main.c").read_text(encoding="utf-8")
        for symbol, _identifier, provider, wrapper, _gate, _predicate in TASK14_ROWS:
            with self.subTest(symbol=symbol):
                self.assertNotIn(f"#define {symbol}", header)
                self.assertNotIn(f"int {provider}(int cmd, unsigned long arg);", proto)
                self.assertNotIn(f"case {symbol}:", driver)
                self.assertNotIn(f"\t{wrapper}();", kernel_main)

    def test_task14_sources_follow_testcase_and_capability_gates(self) -> None:
        driver_make = ROOT / "os/drivers/os_api_test/kernel/Make.defs"
        wrapper_make = ROOT / "apps/examples/testcase/le_tc/kernel/Make.defs"
        for makefile, expected in ((driver_make, TASK14_PROVIDER_SOURCES), (wrapper_make, TASK14_WRAPPER_SOURCES)):
            with self.subTest(makefile=makefile, matrix="enabled"):
                result = selected_sources(makefile, TASK14_FEATURES)
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertTrue(expected.issubset(frozenset(result.stdout.split())), result.stdout)

        disabled_tests = TASK14_FEATURES - {symbol for symbol in TASK14_FEATURES if symbol.startswith("CONFIG_TC_KERNEL_")}
        for makefile, expected in ((driver_make, TASK14_PROVIDER_SOURCES), (wrapper_make, TASK14_WRAPPER_SOURCES)):
            with self.subTest(makefile=makefile):
                result = selected_sources(makefile, disabled_tests)
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertFalse(expected & frozenset(result.stdout.split()), result.stdout)

        no_pipes_procfs = enabled_symbols(ROOT / ".github/scripts/tests/fixtures/os_api_test/no-pipes-no-procfs.config")
        for makefile, forbidden in ((driver_make, {"test_pipe.c", "test_procfs.c"}), (wrapper_make, {"tc_pipe.c", "tc_procfs.c"})):
            with self.subTest(makefile=makefile):
                result = selected_sources(makefile, no_pipes_procfs)
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertFalse(forbidden & frozenset(result.stdout.split()), result.stdout)

    def test_task14_registry_compiles_and_preprocesses_for_enabled_and_disabled_features(self) -> None:
        compiled, preprocessed = registry_translation_unit(TASK14_FEATURES)
        self.assertEqual(0, compiled.returncode, compiled.stderr)
        self.assertEqual(0, preprocessed.returncode, preprocessed.stderr)
        for symbol, _identifier, _provider, _wrapper, _gate, _predicate in TASK14_ROWS:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, preprocessed.stdout)

        disabled = enabled_symbols(ROOT / ".github/scripts/tests/fixtures/os_api_test/no-pipes-no-procfs.config")
        compiled, preprocessed = registry_translation_unit(disabled)
        self.assertEqual(0, compiled.returncode, compiled.stderr)
        self.assertEqual(0, preprocessed.returncode, preprocessed.stderr)
        self.assertNotIn("TESTIOC_PIPE_TEST", preprocessed.stdout)
        self.assertNotIn("TESTIOC_PROCFS_TEST", preprocessed.stdout)

    def test_task14_public_header_compiles_enabled_commands_and_rejects_disabled_ones(self) -> None:
        enabled_source = "#include <tinyara/os_api_test_drv.h>\nint values[] = { TESTIOC_KMM_HEAP_TEST, TESTIOC_MQUEUE_TEST, TESTIOC_ENVIRON_TEST, TESTIOC_ERRNO_TEST, TESTIOC_PROCFS_TEST, TESTIOC_PIPE_TEST, TESTIOC_VFS_TEST, TESTIOC_TERMIOS_TEST };\nint main(void) { return values[0] != 29; }\n"
        result = compile_task14_header(TASK14_FEATURES, enabled_source)
        self.assertEqual(0, result.returncode, result.stderr)

        disabled = enabled_symbols(ROOT / ".github/scripts/tests/fixtures/os_api_test/no-pipes-no-procfs.config")
        for symbol in ("TESTIOC_PIPE_TEST", "TESTIOC_PROCFS_TEST"):
            with self.subTest(symbol=symbol):
                result = compile_task14_header(disabled, f"#include <tinyara/os_api_test_drv.h>\nint value = {symbol};\n")
                self.assertNotEqual(0, result.returncode, result.stderr)


def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[1] != "--root":
        print(f"usage: {Path(argv[0]).name} --root PATH", file=sys.stderr)
        return 2
    root = Path(argv[2]).resolve()
    results: dict[str, CommandResult] = {name: compile_handler(root, name) for name in CONFIGS}
    report = {
        "ok": all(result.returncode == 0 and result.stdout == EXPECTED[name] for name, result in results.items()),
        "configs": {name: asdict(result) for name, result in results.items()},
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
