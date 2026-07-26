#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///

# ─── How to run ───
# python3 -m unittest discover -s .github/scripts/tests -p test_kernel_tc_ioctl_wrappers.py -v

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path
from typing import Final

ROOT: Final = Path(__file__).resolve().parents[3]
FIXTURE: Final = ROOT / ".github/scripts/tests/fixtures/os_api_test/kernel_tc_wrappers"
KERNEL_DIR: Final = ROOT / "apps/examples/testcase/le_tc/kernel"
REGISTRY: Final = ROOT / "os/drivers/os_api_test/os_api_test_kernel_registry.inc"
FEATURES: Final = (
    "CONFIG_TC_KERNEL_PROCFS", "CONFIG_FS_PROCFS", "CONFIG_TC_KERNEL_PIPE", "CONFIG_PIPES",
    "CONFIG_TC_KERNEL_VFS", "CONFIG_TC_KERNEL_WDOG", "CONFIG_WATCHDOG", "CONFIG_TC_KERNEL_LOG_DUMP",
    "CONFIG_LOG_DUMP", "CONFIG_TC_KERNEL_BINARY_MANAGER", "CONFIG_BINARY_MANAGER", "CONFIG_TC_KERNEL_MEM_LEAK_CHECKER",
    "CONFIG_MEM_LEAK_CHECKER", "CONFIG_TC_KERNEL_REBOOT_REASON", "CONFIG_SYSTEM_REBOOT_REASON", "CONFIG_TC_KERNEL_PM",
    "CONFIG_PM", "CONFIG_TC_KERNEL_RTC", "CONFIG_RTC_DRIVER", "CONFIG_TC_KERNEL_BINFMT", "CONFIG_BINFMT_ENABLE",
)


@dataclass(frozen=True, slots=True)
class WrapperContract:
    source: str
    private_name: str
    public_name: str
    command: str
    request: int
    api_name: str


CONTRACTS: Final = (
    WrapperContract("tc_procfs.c", "tc_procfs_kernel", "procfs_main", "TESTIOC_PROCFS_TEST", 47, "procfs"),
    WrapperContract("tc_pipe.c", "tc_pipe_kernel", "pipe_main", "TESTIOC_PIPE_TEST", 49, "pipe"),
    WrapperContract("tc_vfs.c", "tc_vfs_kernel", "vfs_main", "TESTIOC_VFS_TEST", 51, "vfs"),
    WrapperContract("tc_wdog.c", "tc_wdog_kernel", "wdog_main", "TESTIOC_WDOG_TEST", 35, "wdog"),
    WrapperContract("tc_log_dump.c", "tc_log_dump_kernel", "log_dump_main", "TESTIOC_LOG_DUMP_TEST", 42, "log_dump"),
    WrapperContract("tc_binary_manager.c", "tc_binary_manager_kernel", "binary_manager_main", "TESTIOC_BINARY_MANAGER_TEST", 43, "binary_manager"),
    WrapperContract("tc_mem_leak_checker.c", "tc_mem_leak_checker_kernel", "mem_leak_checker_main", "TESTIOC_MEM_LEAK_CHECKER_TEST", 44, "mem_leak_checker"),
    WrapperContract("tc_reboot_reason.c", "tc_reboot_reason_kernel", "reboot_reason_main", "TESTIOC_REBOOT_REASON_TEST", 45, "reboot_reason"),
    WrapperContract("tc_pm.c", "tc_pm_kernel", "pm_tc_main", "TESTIOC_PM_TEST", 46, "pm"),
    WrapperContract("tc_rtc.c", "tc_rtc_kernel", "rtc_main", "TESTIOC_RTC_TEST", 48, "rtc"),
    WrapperContract("tc_binfmt.c", "tc_binfmt_kernel", "binfmt_main", "TESTIOC_BINFMT_TEST", 50, "binfmt"),
)
EXPECTED_REQUESTS: Final = tuple(contract.request for contract in CONTRACTS)


def private_body(contract: WrapperContract, root: Path = ROOT) -> str:
    source = (root / "apps/examples/testcase/le_tc/kernel" / contract.source).read_text(encoding="utf-8")
    match = re.search(rf"static void {contract.private_name}\(void\)\n\{{(?P<body>.*?)\n\}}", source, re.DOTALL)
    if match is None:
        raise AssertionError(f"missing private wrapper {contract.private_name}")
    return match.group("body")


def registry_wrappers(text: str) -> tuple[str, ...]:
    return tuple(re.findall(r"OS_API_TEST_KERNEL_DESCRIPTOR\([^,]+,\s*\d+,\s*[^,]+,\s*[^,]+,\s*([^,]+)", text))


def assert_registry_order(text: str) -> None:
    actual = registry_wrappers(text)
    expected = tuple(contract.public_name for contract in CONTRACTS)
    selected = tuple(wrapper for wrapper in actual if wrapper in expected)
    if selected != expected:
        raise AssertionError(f"registry wrapper order changed: {selected}")


def assert_main_lifecycle(text: str) -> None:
    if text.count("open(OS_API_TEST_DRVPATH, O_WRONLY)") != 1:
        raise AssertionError("kernel_tc_main must own exactly one test-driver open")
    if text.count("close(g_tc_fd)") != 1:
        raise AssertionError("kernel_tc_main must own exactly one test-driver close")
    registry = text.index("#define OS_API_TEST_KERNEL_DESCRIPTOR")
    if not text.rfind("irq_main();", 0, registry) < registry < text.index("itc_environ_main();"):
        raise AssertionError("registry dispatch must remain between legacy and ITC calls")


def compile_harness(root: Path) -> Path:
    fixture = root / "fixture"
    source_dir = root / "apps/examples/testcase/le_tc/kernel"
    source_dir.mkdir(parents=True)
    (root / "apps/examples/testcase").mkdir(exist_ok=True)
    (root / "os/drivers/os_api_test").mkdir(parents=True)
    shutil.copy2(FIXTURE / "tc_common.h", root / "apps/examples/testcase/tc_common.h")
    shutil.copytree(FIXTURE / "include", root / "include")
    shutil.copy2(REGISTRY, root / "os/drivers/os_api_test/os_api_test_kernel_registry.inc")
    for contract in CONTRACTS:
        shutil.copy2(KERNEL_DIR / contract.source, source_dir / contract.source)
    for name in ("kernel_tc_main.c", "tc_internal.h"):
        shutil.copy2(KERNEL_DIR / name, source_dir / name)
    binary = root / "kernel_tc_wrapper_harness"
    command = [
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-value", "-pedantic",
        *(f"-D{feature}" for feature in FEATURES),
        "-Dopen=task16_open", "-Dclose=task16_close", "-Dioctl=task16_ioctl",
        f"-I{root / 'include'}", f"-I{root / 'apps/examples/testcase'}", f"-I{source_dir}",
        str(source_dir / "kernel_tc_main.c"), *(str(source_dir / contract.source) for contract in CONTRACTS),
        str(FIXTURE / "kernel_tc_wrapper_harness.c"), "-o", str(binary),
    ]
    result = subprocess.run(command, capture_output=True, text=True, timeout=20, check=False)
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    return binary


def run_harness(failed_request: int | None = None) -> str:
    with tempfile.TemporaryDirectory(prefix="task16-kernel-tc-") as directory:
        binary = compile_harness(Path(directory))
        command = [str(binary)] if failed_request is None else [str(binary), str(failed_request)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=10, check=False)
        if result.returncode != 0:
            raise AssertionError(result.stderr)
        return result.stdout


class KernelTcIoctlWrapperTest(unittest.TestCase):
    def test_baseline_transcript_when_selected_wrappers_succeed(self) -> None:
        transcript = run_harness()
        self.assertIn("START Kernel TC", transcript)
        self.assertIn("END Kernel TC", transcript)
        for contract in CONTRACTS:
            with self.subTest(wrapper=contract.public_name):
                self.assertIn(f"[{contract.private_name}] PASS", transcript)
        self.assertIn("COUNTS open=1 close=1 close_fd=73 ioctl=11 pass=11 fail=0", transcript)
        self.assertEqual([f"IOCTL fd=73 request={request}" for request in EXPECTED_REQUESTS], [line for line in transcript.splitlines() if line.startswith("IOCTL")])

    def test_shared_helper_keeps_each_wrapper_local_result_format(self) -> None:
        header = (KERNEL_DIR / "tc_internal.h").read_text(encoding="utf-8")
        main = (KERNEL_DIR / "kernel_tc_main.c").read_text(encoding="utf-8")
        self.assertIn("int tc_run_os_api_ioctl(int fd, int request);", header)
        self.assertIn("int tc_run_os_api_ioctl(int fd, int request)", main)
        self.assertIn("return ioctl(fd, request, 0);", main)
        for contract in CONTRACTS:
            with self.subTest(wrapper=contract.public_name):
                body = private_body(contract)
                self.assertIn(f"tc_run_os_api_ioctl(tc_get_drvfd(), {contract.command})", body)
                self.assertIsNone(re.search(r"(?<![A-Za-z0-9_])ioctl\(", body))
                self.assertIn(f'TC_ASSERT_EQ("{contract.api_name}", ret, OK)', body)
                self.assertIn("TC_SUCCESS_RESULT();", body)

    def test_negative_ioctl_is_failure_without_pass_or_lifecycle_drift(self) -> None:
        transcript = run_harness(49)
        self.assertIn("[tc_pipe_kernel] FAIL pipe", transcript)
        self.assertNotIn("[tc_pipe_kernel] PASS", transcript)
        self.assertIn("COUNTS open=1 close=1 close_fd=73 ioctl=11 pass=10 fail=1", transcript)

    def test_registry_order_and_lifecycle_detectors_reject_drift(self) -> None:
        registry = REGISTRY.read_text(encoding="utf-8")
        main = (KERNEL_DIR / "kernel_tc_main.c").read_text(encoding="utf-8")
        assert_registry_order(registry)
        assert_main_lifecycle(main)
        reordered = registry.replace("procfs_main", "task16_placeholder", 1).replace("pipe_main", "procfs_main", 1).replace("task16_placeholder", "pipe_main", 1)
        with self.assertRaises(AssertionError):
            assert_registry_order(reordered)
        with self.assertRaises(AssertionError):
            assert_main_lifecycle(main.replace("close(g_tc_fd);", "close(g_tc_fd);\\n\\tclose(g_tc_fd);", 1))


if __name__ == "__main__":
    unittest.main()
