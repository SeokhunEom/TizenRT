"""Independent raw-byte citation checks for the captured UART corpus.

Use LF positions from raw bytes as the external citation oracle, not the
parser's own line-normalization helper. UART CR CR LF must stay one file line.
"""
import json
from pathlib import Path
import unittest

from tizenrt_debug import analyze
from tizenrt_debug.server import DebugService


class PhysicalCorpusCitationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1] / "examples/qemu-armv8m"
        metadata = json.loads((cls.root / "manifest.json").read_text())
        cls.raw_logs = {item["case"]: (cls.root / item["log"]).read_bytes() for item in metadata["cases"]}
        cls.paths = {item["case"]: cls.root / item["log"] for item in metadata["cases"]}

    @staticmethod
    def physical_lines(raw):
        lines = raw.split(b"\n")
        if raw.endswith(b"\n"):
            lines.pop()
        return [line.rstrip(b"\r").decode("utf-8", "replace") for line in lines]

    def test_uart_cr_cr_lf_does_not_invent_file_lines(self):
        self.assertTrue(any(b"\r\r\n" in raw for raw in self.raw_logs.values()))
        for case, raw in self.raw_logs.items():
            with self.subTest(case=case):
                expected = self.physical_lines(raw)
                report = analyze(raw.decode("utf-8", "replace"))
                self.assertEqual(len(expected), report["line_count"])
                for event in report["events"]:
                    self.assertEqual(expected[event["line"] - 1], event["raw"])

    def test_fault_and_assertion_citations_match_raw_lf_positions(self):
        checked = 0
        for case, raw in self.raw_logs.items():
            expected = self.physical_lines(raw)
            report = analyze(raw.decode("utf-8", "replace"))
            for event in report["events"]:
                if event["kind"] not in {"fault", "assertion"}:
                    continue
                with self.subTest(case=case, kind=event["kind"]):
                    occurrences = [index + 1 for index, line in enumerate(expected) if line == event["raw"]]
                    self.assertIn(event["line"], occurrences)
                    checked += 1
        self.assertGreater(checked, 10)

    def test_mcp_excerpt_returns_the_same_physical_citation_line(self):
        service = DebugService([self.root])
        for case in ("memfault", "stack", "heapcorrupt", "oom"):
            with self.subTest(case=case):
                result = service.call("analyze_log", {"path": str(self.paths[case])})
                expected = self.physical_lines(self.raw_logs[case])
                assertion_line = next(index + 1 for index, line in enumerate(expected)
                                      if "Assertion failed at file:" in line)
                excerpt = service.call("get_log_excerpt", {
                    "report_id": result["report_id"], "start_line": assertion_line, "count": 1,
                })
                self.assertEqual(assertion_line, excerpt["items"][0]["line"])
                self.assertEqual(expected[assertion_line - 1], excerpt["items"][0]["text"])


if __name__ == "__main__":
    unittest.main()
