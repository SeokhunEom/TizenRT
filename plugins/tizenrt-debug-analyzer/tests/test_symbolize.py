from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tizenrt_debug import analyze
from tizenrt_debug.symbolize import addresses, symbolize, symbolize_map


class SymbolizeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.elf = self.root / "firmware.elf"
        self.elf.write_bytes(b"\x7fELFtest")
        self.tool = self.root / "arm-none-eabi-addr2line"
        self.tool.write_text("#!/bin/sh\nprintf 'worker\\na.c:12\\n'\n")
        self.tool.chmod(0o755)
        self.report = analyze("PANIC!!! Usagefault at instruction: 0x10001235\n")

    def test_explicit_tool_and_address_argument_vector(self):
        completed = subprocess.CompletedProcess([], 0, "worker\na.c:12\n", "")
        with patch("subprocess.run", return_value=completed) as run:
            result = symbolize(self.report, str(self.elf), str(self.tool))
        command = run.call_args.args[0]
        self.assertEqual([str(self.tool), "-f", "-C", "-e", str(self.elf), "0x10001234"], command)
        self.assertIs(False, run.call_args.kwargs["shell"])
        self.assertEqual(5, run.call_args.kwargs["timeout"])
        self.assertTrue(result["symbolization"]["entries"][0]["resolved"])
        self.assertNotIn("symbolization", self.report)

    def test_relative_tool_and_non_addr2line_are_rejected(self):
        with self.assertRaises(ValueError):
            symbolize(self.report, str(self.elf), "addr2line")
        malicious = self.root / "bash"
        malicious.write_text("anything")
        malicious.chmod(0o755)
        with self.assertRaises(ValueError):
            symbolize(self.report, str(self.elf), str(malicious))

    def test_non_elf_and_nonpositive_timeout_are_rejected(self):
        self.elf.write_text("not an ELF")
        with self.assertRaises(ValueError):
            symbolize(self.report, str(self.elf), str(self.tool))
        with self.assertRaises(ValueError):
            symbolize(self.report, str(self.elf), str(self.tool), timeout=0)

    def test_timeout_is_not_reported_as_success(self):
        with patch("subprocess.run", side_effect=subprocess.TimeoutExpired("addr2line", 5)):
            with self.assertRaises(subprocess.TimeoutExpired):
                symbolize(self.report, str(self.elf), str(self.tool))

    def test_wrong_output_shape_is_rejected(self):
        with patch("subprocess.run", return_value=subprocess.CompletedProcess([], 0, "wrong\n", "")):
            with self.assertRaises(ValueError):
                symbolize(self.report, str(self.elf), str(self.tool))

    def test_map_candidates_are_not_claimed_verified(self):
        mapping = self.root / "firmware.map"
        mapping.write_text("10001000 T first\n                0x10001200 worker\n10002000 T next\n")
        result = symbolize_map(self.report, str(mapping))
        entry = result["symbolization"]["entries"][0]
        self.assertEqual("worker", entry["symbol"])
        self.assertEqual(0x34, entry["offset"])
        self.assertFalse(entry["resolved"])
        self.assertFalse(result["symbolization"]["build_identity_verified"])

    def test_stack_words_are_not_symbolization_candidates(self):
        report = analyze("User thread stack dump:\n80001000: 10001235 deadbeef 00000000\n")
        self.assertEqual([], addresses(report))

    def test_hostile_report_address_is_rejected_before_subprocess(self):
        self.report["events"][0]["data"]["instruction"] = "$(touch /tmp/no)"
        with patch("subprocess.run", side_effect=AssertionError("must not execute")):
            with self.assertRaises(ValueError):
                symbolize(self.report, str(self.elf), str(self.tool))


if __name__ == "__main__":
    unittest.main()
