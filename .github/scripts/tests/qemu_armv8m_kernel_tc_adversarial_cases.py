from __future__ import annotations

import dataclasses
import math

from qemu_armv8m_kernel_tc_test_support import RunnerHarness


PASS_EXIT_SEVEN = (
    "import sys\n"
    "sys.stdout.write('Kernel TC End [PASS : 7, FAIL : 0]\\n')\n"
    "sys.stdout.flush()\n"
    "sys.exit(7)\n"
)

STALE_PROMPT = (
    "import sys\n"
    "import time\n"
    "sys.stdout.write('TASH>>')\n"
    "sys.stdout.flush()\n"
    "assert sys.stdin.readline() == 'kernel_tc\\n'\n"
    "sys.stdout.write('TASH: cmd (kernel_tc) not registered\\n')\n"
    "sys.stdout.flush()\n"
    "time.sleep(1)\n"
)

FRAGMENTED_FRESH_PROMPT = (
    "import select\n"
    "import sys\n"
    "sys.stdout.write('TASH>>')\n"
    "sys.stdout.flush()\n"
    "assert sys.stdin.readline() == 'kernel_tc\\n'\n"
    "sys.stdout.write('TASH: cmd (kernel_tc) not registered\\nTA')\n"
    "sys.stdout.flush()\n"
    "assert not select.select([sys.stdin], [], [], 0.1)[0]\n"
    "sys.stdout.write('SH>>')\n"
    "sys.stdout.flush()\n"
    "assert sys.stdin.readline() == 'kernel_tc\\n'\n"
    "sys.stdout.write('Kernel TC End [PASS : 2, FAIL : 0]\\n')\n"
    "sys.stdout.flush()\n"
)


class QemuArmv8mKernelTcAdversarialTest(RunnerHarness):
    def test_unregistered_marker_cannot_reuse_a_stale_prompt(self) -> None:
        request = dataclasses.replace(self.request(), timeout_sec=0.2)

        code = self.runner.run_kernel_tc(request, self.child_command(STALE_PROMPT))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("timeout", result["reason"])
        self.assertEqual(1, result["send_count"])

    def test_fragmented_fresh_prompt_retries_after_unregistered_marker(self) -> None:
        code = self.runner.run_kernel_tc(self.request(), self.child_command(FRAGMENTED_FRESH_PROMPT))
        result = self.read_result()

        self.assertEqual(0, code)
        self.assertEqual("pass", result["reason"])
        self.assertEqual(2, result["send_count"])

    def test_nonzero_exit_after_positive_marker_is_not_a_pass(self) -> None:
        code = self.runner.run_kernel_tc(self.request(), self.child_command(PASS_EXIT_SEVEN))
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("qemu-exit", result["reason"])
        self.assertEqual(7, result["returncode"])

    def test_required_marker_evidence_survives_serial_window_rollover(self) -> None:
        script = (
            "import sys\n"
            "import time\n"
            "sys.stdout.write('QEMU_LOAD_REJECT common crc\\n')\n"
            "sys.stdout.flush()\n"
            "sys.stdout.buffer.write(b'x' * 10000)\n"
            "sys.stdout.buffer.flush()\n"
            "time.sleep(1)\n"
        )
        request = dataclasses.replace(
            self.request(),
            expect_reject="QEMU_LOAD_REJECT common",
            forbid_marker="QEMU_APP1_STARTED",
            reject_observe_seconds=0.2,
        )

        code = self.runner.run_kernel_tc(request, self.child_command(script))
        result = self.read_result()

        self.assertEqual(0, code)
        self.assertEqual("expected-rejection", result["status"])
        self.assertTrue(result["required_marker_seen"])
        self.assertFalse(result["forbidden_marker_seen"])

    def test_empty_or_whitespace_markers_are_preflight_errors(self) -> None:
        for marker in ("", " \t"):
            with self.subTest(field="expect_reject", marker=marker):
                request = dataclasses.replace(
                    self.request(),
                    expect_reject=marker,
                    forbid_marker="QEMU_APP1_STARTED",
                )
                self.assertEqual(1, self.runner.run_kernel_tc(request, self.child_command("")))
                self.assertEqual("preflight", self.read_result()["reason"])
            with self.subTest(field="forbid_marker", marker=marker):
                request = dataclasses.replace(
                    self.request(),
                    expect_reject="QEMU_LOAD_REJECT common",
                    forbid_marker=marker,
                )
                self.assertEqual(1, self.runner.run_kernel_tc(request, self.child_command("")))
                self.assertEqual("preflight", self.read_result()["reason"])

    def test_nonfinite_timeouts_are_preflight_errors(self) -> None:
        for timeout in (math.nan, math.inf, -math.inf):
            with self.subTest(timeout=timeout):
                request = dataclasses.replace(self.request(), timeout_sec=timeout)
                self.assertEqual(1, self.runner.run_kernel_tc(request, self.child_command("")))
                self.assertEqual("preflight", self.read_result()["reason"])
