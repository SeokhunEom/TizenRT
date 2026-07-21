from __future__ import annotations

import shlex
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

from mem_leak_checker_qa_core import QaError  # noqa: E402
from mem_leak_checker_qemu_qa import (  # noqa: E402
    BRANCH,
    TASK3_BRANCH,
    _run_task3_descriptor_scenario,
    _task3_context_mode,
)


class Task3PostIntegrationRegressionTests(unittest.TestCase):
    def test_context_modes_reject_cross_mode_path_branch_identities(self) -> None:
        main = Path("/trusted/main")
        todo = Path("/trusted/todo3")

        self.assertEqual(_task3_context_mode(todo, main, todo, TASK3_BRANCH), "development")
        self.assertEqual(_task3_context_mode(main, main, todo, BRANCH), "receiving")
        with self.assertRaises(QaError):
            _task3_context_mode(main, main, todo, TASK3_BRANCH)
        with self.assertRaises(QaError):
            _task3_context_mode(todo, main, todo, BRANCH)

    def test_scenario_receipt_exit_is_the_direct_subprocess_exit(self) -> None:
        command_argv = [sys.executable, "-c", "raise SystemExit(7)"]
        descriptor = {
            "command": " ".join(shlex.quote(item) for item in command_argv),
            "expected_exit": 7,
        }

        with tempfile.TemporaryDirectory(prefix="mlc-task3-scenario-test-") as directory:
            result = _run_task3_descriptor_scenario(
                Path.cwd(), Path(directory), descriptor
            )

        self.assertEqual(result["command_argv"], command_argv)
        self.assertEqual(result["exit"], 7)

    def test_scenario_seal_rejects_exit_different_from_direct_command(self) -> None:
        command_argv = [sys.executable, "-c", "raise SystemExit(7)"]
        descriptor = {
            "command": " ".join(shlex.quote(item) for item in command_argv),
            "expected_exit": 0,
        }

        with tempfile.TemporaryDirectory(prefix="mlc-task3-scenario-test-") as directory:
            with self.assertRaisesRegex(QaError, "expected 0, observed 7"):
                _run_task3_descriptor_scenario(Path.cwd(), Path(directory), descriptor)

    def test_scenario_seal_rejects_recursive_seal_command(self) -> None:
        descriptor = {
            "command": "tools/mem_leak_checker_qa.sh seal-task --task 3 --source HEAD",
            "expected_exit": 0,
        }

        with tempfile.TemporaryDirectory(prefix="mlc-task3-scenario-test-") as directory:
            with self.assertRaisesRegex(QaError, "recursion rejected"):
                _run_task3_descriptor_scenario(Path.cwd(), Path(directory), descriptor)


if __name__ == "__main__":
    unittest.main()
