"""Regression against archived real QEMU output, without launching QEMU.

Source checkouts use the pinned corpus. Installed plugin packages without the
repository corpus skip explicitly. TIZENRT_CORPUS_DIR can select another archive;
an explicitly selected missing/incomplete archive is an error, not a skip.
"""

import hashlib
import json
import os
from pathlib import Path
import re
import unittest

from tizenrt_debug import analyze, log_lines


class ActualQemuCorpusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        default = Path(__file__).resolve().parents[1] / "examples/qemu-armv8m"
        cls.root = Path(os.environ.get("TIZENRT_CORPUS_DIR", default))
        manifest = cls.root / "manifest.json"
        if not manifest.is_file():
            manifest = cls.root / "summary.json"
        if not manifest.is_file() and "TIZENRT_CORPUS_DIR" not in os.environ:
            raise unittest.SkipTest("real QEMU corpus not included in this installed package")
        cls.summary = json.loads(manifest.read_text())
        records = cls.summary["results"] if "results" in cls.summary else cls.summary["cases"]
        cls.records = {item["case"]: item for item in records}
        cls.texts = {}
        cls.reports = {}
        for case, item in cls.records.items():
            text = (cls.root / item["log"]).read_bytes().decode("utf-8", "replace")
            cls.texts[case] = text
            cls.reports[case] = analyze(text, source=item["log"])

    def kinds(self, case):
        return {event["kind"] for event in self.reports[case]["events"]}

    def flags(self, case):
        return {flag["name"] for incident in self.reports[case]["incidents"]
                for status in incident["fault_status"] for flag in status["flags"]}

    def test_archived_raw_hashes_and_capture_observations(self):
        self.assertEqual(24, len(self.records))
        self.assertIn("kernel_tc", self.records)
        for case, item in self.records.items():
            with self.subTest(case=case):
                raw = (self.root / item["log"]).read_bytes()
                self.assertEqual(item["log_sha256"], hashlib.sha256(raw).hexdigest())
                self.assertEqual(item["log_bytes"], len(raw))
                self.assertEqual("actual-qemu-runtime", item.get("evidence_kind", self.summary.get("evidence_kind")))
                self.assertTrue(item["expected_observed"])
                self.assertFalse(item["observation_timed_out"])
                self.assertIs(False, item.get("hardware_validated", self.summary.get("hardware_validated")))
                self.assertIsNotNone(re.search(item["expected_pattern"], self.texts[case], re.I))

    def test_every_event_retains_its_exact_physical_line(self):
        for case, report in self.reports.items():
            lines = log_lines(self.texts[case])
            self.assertEqual(len(lines), report["line_count"])
            for event in report["events"]:
                with self.subTest(case=case, line=event["line"]):
                    self.assertEqual(lines[event["line"] - 1], event["raw"])

    def test_diagnostic_tables_have_structured_observations(self):
        expected = {
            "boot": {"boot", "shell_prompt"}, "ps": {"ps_task"}, "free": {"free_summary"},
            "heapinfo": {"heap_metric", "heap_free_bucket", "heap_thread"},
            "heapinfo_all": {"heap_metric", "heap_node"}, "heapinfo_free": {"heap_node"},
            "heapinfo_regions": {"heap_metric"}, "stackmonitor": {"stackmonitor_task"},
            "histories": {"history_header", "history_record"},
        }
        for case, expected_kinds in expected.items():
            with self.subTest(case=case):
                self.assertTrue(expected_kinds <= self.kinds(case))
                self.assertEqual("no_recognized_failure", self.reports[case]["summary"]["verdict"])

    def test_network_sample_remains_gpt_context_not_false_failure(self):
        report = self.reports["network_interfaces"]
        self.assertIn("eth0", self.texts["network_interfaces"])
        self.assertTrue(report["unknown_lines"])
        self.assertEqual("no_recognized_failure", report["summary"]["verdict"])

    def test_actual_fault_kinds_and_status_flags(self):
        expected = {
            "udf": ("usage_fault", {"UNDEFINSTR"}), "divzero": ("usage_fault", {"DIVBYZERO"}),
            "unaligned": ("usage_fault", {"UNALIGNED"}), "invstate": ("usage_fault", {"INVSTATE"}),
            "busfault": ("bus_fault", {"PRECISERR", "BFARVALID"}),
            "hardfault": ("hard_fault", {"FORCED", "UNDEFINSTR"}),
            "memfault": ("memmanage_fault", {"DACCVIOL", "MMARVALID"}),
            "execfault": ("memmanage_fault", {"IACCVIOL"}), "stack": ("usage_fault", {"STKOF"}),
        }
        for case, (kind, expected_flags) in expected.items():
            with self.subTest(case=case):
                report = self.reports[case]
                self.assertEqual(kind, report["incidents"][0]["kind"])
                self.assertTrue(expected_flags <= self.flags(case))
                self.assertIn("assertion", self.kinds(case))
                self.assertEqual("issues_observed", report["summary"]["verdict"])

    def test_real_fault_address_validity(self):
        expected = (("busfault", "BFAR", True), ("memfault", "MMFAR", True),
                    ("execfault", "MMFAR", False), ("hardfault", "BFAR", False))
        for case, address, valid in expected:
            with self.subTest(case=case):
                records = [s["addresses"][address] for i in self.reports[case]["incidents"]
                           for s in i["fault_status"] if address in s["addresses"]]
                self.assertTrue(records)
                self.assertIs(valid, records[0]["valid"])

    def test_allocator_and_assert_observations(self):
        expected = {"assert": {"assertion"}, "oom": {"allocation_failure", "allocation_detail", "heap_caller"},
                    "doublefree": {"invalid_free", "heap_caller"}, "heapcorrupt": {"heap_corruption"}}
        for case, required in expected.items():
            with self.subTest(case=case):
                self.assertTrue(required <= self.kinds(case))
                self.assertEqual("issues_observed", self.reports[case]["summary"]["verdict"])

    def test_stack_fault_keeps_accompanying_heap_corruption(self):
        self.assertIn("STKOF", self.flags("stack"))
        self.assertIn("heap_corruption", self.kinds("stack"))
        self.assertTrue(any(f["hypothesis"] for f in self.reports["stack"]["findings"]))

    def test_guest_reset_closes_incident_before_next_boot(self):
        for case, item in self.records.items():
            if not item["guest_reboot_requested"]:
                continue
            with self.subTest(case=case):
                report = self.reports[case]
                self.assertTrue(any(i["end_reason"] == "reboot_request" for i in report["incidents"]))
                self.assertGreaterEqual(report["summary"]["boot_banner_count"], 2)
                reset = next(e["line"] for e in report["events"] if e["kind"] == "reboot_request")
                for event in report["events"]:
                    if event["line"] > reset:
                        self.assertIsNone(event["incident_id"])

    def test_history_retains_names_with_spaces_and_semaphore_statuses(self):
        records = [e["data"] for e in self.reports["histories"]["history"] if e["kind"] == "history_record"]
        self.assertEqual({"task_scheduling", "irq_scheduling", "semaphore"}, {r["history"] for r in records})
        self.assertTrue(any(r.get("name") == "Idle Task" and r.get("pid") == 0 for r in records))
        self.assertTrue(any(r.get("status") in {"AQ", "RL", "WT"} for r in records))

    def test_kernel_tc_summary_when_captured(self):
        report = self.reports["kernel_tc"]
        summaries = [s for s in report["summary"]["test_summaries"] if s["suite"] == "Kernel TC"]
        self.assertEqual(1, len(summaries))
        self.assertEqual(459, summaries[0]["pass"])
        self.assertEqual(0, summaries[0]["fail"])
        self.assertEqual("no_recognized_failure", report["summary"]["verdict"])
        self.assertEqual([], report["incidents"])


if __name__ == "__main__":
    unittest.main()
