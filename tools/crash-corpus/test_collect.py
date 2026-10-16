"""Regression checks for evidence recognition, including command echo negatives."""
import json
from pathlib import Path
import re
import unittest

from collect import DIAGNOSTICS


class CorpusRecognitionTests(unittest.TestCase):
    def test_command_echo_and_error_are_not_observed_diagnostics(self):
        for case, (commands, expected) in DIAGNOSTICS.items():
            if not commands:
                continue
            with self.subTest(case=case):
                log = "TASH>>\x00" + commands[-1] + "\ncommand is not registered\nTASH>>\x00"
                self.assertIsNone(re.search(expected, log, re.I))

    def test_curated_real_logs_match_current_strict_diagnostics(self):
        root = Path(__file__).parent / "evidence/20261016-qemu-armv8m"
        summary = json.loads((root / "summary.json").read_text())
        for result in summary["results"]:
            with self.subTest(case=result["case"]):
                text = (root / result["log"]).read_text()
                if result["case"] != "boot":
                    # Require data after this run's actual diagnostic command.
                    command = result["serial_commands"][-1]["command"]
                    text = text.split("TASH>>\x00" + command, 1)[1]
                self.assertRegex(text, re.compile(DIAGNOSTICS[result["case"]][1], re.I))


if __name__ == "__main__":
    unittest.main()
