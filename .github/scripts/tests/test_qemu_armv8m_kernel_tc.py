from __future__ import annotations

import contextlib
import dataclasses
import importlib
import io
import os
import shutil
import json
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from qemu_armv8m_kernel_tc_test_support import RunnerHarness
from qemu_armv8m_ab import flip_active_slot, stage_state


PASS = "import sys; sys.stdout.buffer.write(b'Kernel TC End [PASS : 3, FAIL : 0]\\n'); sys.stdout.flush()"
PASS_ZERO = "import sys; sys.stdout.buffer.write(b'Kernel TC End [PASS : 0, FAIL : 0]\\n'); sys.stdout.flush()"
FAIL = "import sys; sys.stdout.buffer.write(b'Kernel TC End [PASS : 3, FAIL : 1]\\n'); sys.stdout.flush()"


class QemuArmv8mKernelTcTest(RunnerHarness):
    def prepare_ab_request(self):
        config_root = self.root / "build/configs/qemu-armv8m/loadable_all"
        config_root.mkdir(parents=True)
        shutil.copy2(
            Path(__file__).resolve().parents[3] / "build/configs/qemu-armv8m/loadable_all/defconfig",
            config_root / "defconfig",
        )
        bin_dir = self.root / "build/output/bin"
        bin_dir.mkdir(parents=True)
        for name in ("tinyara.bin", "common", "app1", "app2"):
            data = bytearray(16 + len(name))
            data[4:6] = (12).to_bytes(2, "little")
            data[10:14] = len(name).to_bytes(4, "little")
            data[16:] = name.encode()
            (bin_dir / name).write_bytes(data)
        state = self.root / "state.bin"
        stage_state(self.root, "loadable_all", state)
        request = dataclasses.replace(
            self.request(),
            config="loadable_all",
            log_path=self.root / "artifacts/serial.log",
            result_path=self.root / "artifacts/result.json",
            max_reboots=1,
        )
        return request, state

    def test_ab_retry_follows_binary_manager_slot_change(self) -> None:
        request, state = self.prepare_ab_request()
        seen_slots = []

        def fake_protocol(attempt_request, _command_builder):
            seen_slots.append(self.runner.active_slot(state, self.root, "loadable_all"))
            attempt_request.result_path.parent.mkdir(parents=True, exist_ok=True)
            if len(seen_slots) == 1:
                flip_active_slot(state, self.root, "loadable_all")
                attempt_request.result_path.write_text(
                    json.dumps({"status": "failed", "reason": "qemu-exit", "returncode": 0}),
                    encoding="utf-8",
                )
                return 1
            attempt_request.result_path.write_text(
                json.dumps({"status": "pass", "reason": "pass", "returncode": 0}),
                encoding="utf-8",
            )
            return 0

        original = self.runner.run_protocol
        self.runner.run_protocol = fake_protocol
        try:
            code = self.runner._run_ab_attempts(request, state)
        finally:
            self.runner.run_protocol = original

        self.assertEqual(0, code)
        self.assertEqual([0, 1], seen_slots)

    def test_ab_retry_does_not_mask_timeout(self) -> None:
        request, state = self.prepare_ab_request()
        attempts = []

        def fake_protocol(attempt_request, _command_builder):
            attempts.append(attempt_request)
            attempt_request.result_path.parent.mkdir(parents=True, exist_ok=True)
            attempt_request.result_path.write_text(
                json.dumps({"status": "failed", "reason": "timeout", "returncode": None}),
                encoding="utf-8",
            )
            return 1

        original = self.runner.run_protocol
        self.runner.run_protocol = fake_protocol
        try:
            code = self.runner._run_ab_attempts(request, state)
        finally:
            self.runner.run_protocol = original

        self.assertEqual(1, code)
        self.assertEqual(1, len(attempts))

    def test_qemu_command_covers_layouts_and_alternate_packages(self) -> None:
        _tinyara, common, app1 = self.write_packages()

        hello = self.runner.qemu_command(self.root, "hello")
        loadable = self.runner.qemu_command(self.root, "loadable_all", app1_path=app1)
        loadable_apps = self.runner.qemu_command(self.root, "loadable_apps", app1_path=app1)
        xip = self.runner.qemu_command(self.root, "xip_all", common_path=common, app1_path=app1)
        omitted = self.runner.qemu_command(self.root, "xip_all", app1_path=app1, omit_common=True)

        self.assertNotIn("-device", hello)
        self.assertIn(f"loader,file={app1},addr=0x10300000,force-raw=on", loadable)
        self.assertIn(f"loader,file={app1},addr=0x10300000,force-raw=on", loadable_apps)
        self.assertIn(f"loader,file={common},addr=0x102c0000,force-raw=on", xip)
        self.assertIn(f"loader,file={app1},addr=0x10360000,force-raw=on", xip)
        self.assertNotIn("addr=0x102c0000,force-raw=on", " ".join(omitted))

    def test_preflight_failure_still_creates_log_and_result(self) -> None:
        def missing_image(_request):
            raise FileNotFoundError("missing kernel image fixture")

        code = self.runner.run_kernel_tc(self.request(), missing_image)
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertTrue(self.log_path.is_file())
        self.assertEqual("preflight", result["reason"])

    def test_fragmented_positive_pass_writes_a_pass_result(self) -> None:
        script = (
            "import sys\n"
            "sys.stdout.buffer.write(b'Kernel TC End [PASS : ')\n"
            "sys.stdout.buffer.flush()\n"
            "sys.stdout.buffer.write(b'3, FAIL : 0]\\n')\n"
            "sys.stdout.buffer.flush()\n"
        )

        code = self.runner.run_kernel_tc(self.request(), self.child_command(script))
        result = self.read_result()

        self.assertEqual(0, code)
        self.assertEqual("pass", result["reason"])
        self.assertEqual(3, result["pass_count"])

    def test_exact_once_marker_accepts_one_serial_occurrence(self) -> None:
        script = "import sys; sys.stdout.write('[tc_owned] PASS\\nKernel TC End [PASS : 1, FAIL : 0]\\n'); sys.stdout.flush()"
        request = dataclasses.replace(self.request(), expected_once=("[tc_owned] PASS",))

        code = self.runner.run_kernel_tc(request, self.child_command(script))
        result = self.read_result()

        self.assertEqual(0, code)
        self.assertEqual("pass", result["reason"])

    def test_exact_once_marker_rejects_duplicate_serial_occurrences(self) -> None:
        script = "import sys; sys.stdout.write('[tc_owned] PASS\\n[tc_owned] PASS\\nKernel TC End [PASS : 2, FAIL : 0]\\n'); sys.stdout.flush()"
        request = dataclasses.replace(self.request(), expected_once=("[tc_owned] PASS",))

        code = self.runner.run_kernel_tc(request, self.child_command(script))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("marker-count", result["reason"])

    def test_empty_exact_once_marker_is_rejected_before_spawn(self) -> None:
        request = dataclasses.replace(self.request(), expected_once=(" ",))

        code = self.runner.run_kernel_tc(request, self.child_command(PASS))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("preflight", result["reason"])

    def test_prompt_retry_after_unregistered_command_reaches_pass(self) -> None:
        script = (
            "import sys\n"
            "sys.stdout.write('TASH>>')\n"
            "sys.stdout.flush()\n"
            "assert sys.stdin.readline() == 'kernel_tc\\n'\n"
            "sys.stdout.write('TASH: cmd (kernel_tc) not registered\\nTASH>>')\n"
            "sys.stdout.flush()\n"
            "assert sys.stdin.readline() == 'kernel_tc\\n'\n"
            "sys.stdout.write('Kernel TC End [PASS : 2, FAIL : 0]\\n')\n"
            "sys.stdout.flush()\n"
        )

        code = self.runner.run_kernel_tc(self.request(), self.child_command(script))
        result = self.read_result()

        self.assertEqual(0, code)
        self.assertEqual(2, result["send_count"])

    def test_zero_pass_zero_fail_is_a_protocol_error(self) -> None:
        code = self.runner.run_kernel_tc(self.request(), self.child_command(PASS_ZERO))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("protocol-error", result["reason"])
        self.assertEqual(0, result["pass_count"])

    def test_positive_fail_is_a_kernel_tc_failure(self) -> None:
        code = self.runner.run_kernel_tc(self.request(), self.child_command(FAIL))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("kernel-tc-fail", result["reason"])
        self.assertEqual(1, result["fail_count"])

    def test_early_exit_records_tail_and_qemu_exit(self) -> None:
        script = "import sys; sys.stdout.write('diagnostic-tail\\n'); sys.stdout.flush()"
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            code = self.runner.run_kernel_tc(self.request(), self.child_command(script))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("qemu-exit", result["reason"])
        self.assertIn("diagnostic-tail", stderr.getvalue())

    def test_timeout_terminates_child_before_late_output(self) -> None:
        pid_path = self.root / "child.pid"
        script = (
            "import os\n"
            "import sys\n"
            "import time\n"
            "from pathlib import Path\n"
            "Path(sys.argv[1]).write_text(str(os.getpid()), encoding='utf-8')\n"
            "sys.stdout.write('started\\n')\n"
            "sys.stdout.flush()\n"
            "time.sleep(3)\n"
            "sys.stdout.write('late-output\\n')\n"
            "sys.stdout.flush()\n"
        )
        request = dataclasses.replace(self.request(), timeout_sec=0.5)

        code = self.runner.run_kernel_tc(request, self.child_command(script, str(pid_path)))
        result = self.read_result()
        time.sleep(0.1)

        self.assertEqual(1, code)
        self.assertEqual("timeout", result["reason"])
        self.assertNotIn(b"late-output", self.log_path.read_bytes())
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pid_path.read_text(encoding="utf-8")), 0)

    def test_invalid_timeout_is_rejected_before_qemu_spawn(self) -> None:
        request = dataclasses.replace(self.request(), timeout_sec=0.0)
        code = self.runner.run_kernel_tc(request, self.child_command(PASS))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("preflight", result["reason"])
        self.assertTrue(self.log_path.is_file())

    def test_out_of_range_observation_window_is_rejected_before_qemu_spawn(self) -> None:
        request = dataclasses.replace(
            self.request(),
            expect_reject="QEMU_LOAD_REJECT common",
            forbid_marker="QEMU_APP1_STARTED",
            reject_observe_seconds=61.0,
        )
        code = self.runner.run_kernel_tc(request, self.child_command(PASS))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("preflight", result["reason"])

    def test_reject_followed_by_app_start_is_not_expected_rejection(self) -> None:
        script = (
            "import sys\n"
            "import time\n"
            "sys.stdout.write('QEMU_LOAD_REJECT common crc\\nQEMU_APP1_STARTED pid=7\\n')\n"
            "sys.stdout.flush()\n"
            "time.sleep(1)\n"
        )
        request = dataclasses.replace(
            self.request(),
            expect_reject="QEMU_LOAD_REJECT common",
            forbid_marker="QEMU_APP1_STARTED",
        )

        code = self.runner.run_kernel_tc(request, self.child_command(script))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("protocol-error", result["reason"])
        self.assertTrue(result["forbidden_marker_seen"])

    def test_reject_followed_by_process_exit_is_expected_rejection(self) -> None:
        script = "import sys; sys.stdout.write('QEMU_LOAD_REJECT app1 crc\\n'); sys.stdout.flush()"
        request = dataclasses.replace(
            self.request(),
            expect_reject="QEMU_LOAD_REJECT app1",
            forbid_marker="QEMU_APP1_STARTED",
            reject_observe_seconds=0.2,
        )

        code = self.runner.run_kernel_tc(request, self.child_command(script))
        result = self.read_result()

        self.assertEqual(0, code)
        self.assertEqual("expected-rejection", result["reason"])
        self.assertEqual(0, result["returncode"])

    def test_reject_followed_by_nonzero_process_exit_fails(self) -> None:
        script = "import sys; sys.stdout.write('QEMU_LOAD_REJECT app1 crc\\n'); sys.stdout.flush(); sys.exit(7)"
        request = dataclasses.replace(
            self.request(),
            expect_reject="QEMU_LOAD_REJECT app1",
            forbid_marker="QEMU_APP1_STARTED",
        )

        code = self.runner.run_kernel_tc(request, self.child_command(script))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("qemu-exit", result["reason"])
        self.assertEqual(7, result["returncode"])

    def test_expected_rejection_requires_quiet_observation_window(self) -> None:
        script = (
            "import sys\n"
            "import time\n"
            "sys.stdout.write('QEMU_LOAD_REJECT common crc\\n')\n"
            "sys.stdout.flush()\n"
            "time.sleep(1)\n"
        )
        request = dataclasses.replace(
            self.request(),
            expect_reject="QEMU_LOAD_REJECT common",
            forbid_marker="QEMU_APP1_STARTED",
            reject_observe_seconds=0.05,
        )

        code = self.runner.run_kernel_tc(request, self.child_command(script))
        result = self.read_result()

        self.assertEqual(0, code)
        self.assertEqual("expected-rejection", result["status"])
        self.assertTrue(result["required_marker_seen"])
        self.assertFalse(result["forbidden_marker_seen"])


def load_tests(
    loader: unittest.TestLoader,
    tests: unittest.TestSuite,
    _pattern: str | None,
) -> unittest.TestSuite:
    module = importlib.import_module("qemu_armv8m_kernel_tc_adversarial_cases")
    tests.addTests(loader.loadTestsFromTestCase(module.QemuArmv8mKernelTcAdversarialTest))
    return tests
