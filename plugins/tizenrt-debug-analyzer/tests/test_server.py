import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tizenrt_debug.server import DebugService, Protocol

ROOT = Path(__file__).resolve().parents[1]
FAULT = "up_usagefault: Usage Fault: CFAULTS: 02000000\nAssertion failed at file:up_usagefault.c line: 100\n"


class ServiceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name).resolve()
        self.service = DebugService([self.root])

    def tearDown(self):
        self.tmp.cleanup()

    def test_log_file_snapshot_and_lines(self):
        path = self.root / "uart.log"
        path.write_text(FAULT)
        result = self.service.call("analyze_log", {"path": str(path)})
        path.write_text("changed\n")
        excerpt = self.service.call("get_log_excerpt", {"report_id": result["report_id"], "start_line": 1})
        self.assertEqual(excerpt["items"][0]["text"], FAULT.splitlines()[0])
        self.assertEqual(excerpt["sha256"], result["sha256"])
        self.assertEqual(result["summary"]["incident_count"], 1)

    def test_no_roots_text_works_and_files_are_rejected(self):
        service = DebugService()
        result = service.call("analyze_log", {"text": FAULT})
        self.assertEqual(result["line_count"], 2)
        with self.assertRaises(ValueError):
            service.call("analyze_log", {"path": str(ROOT / "catalog.json")})

    def test_symlink_outside_roots_is_rejected(self):
        (self.root / "outside").symlink_to(ROOT / "catalog.json")
        with self.assertRaises(ValueError):
            self.service.call("analyze_log", {"path": str(self.root / "outside")})

    def test_bad_arguments_are_actionable(self):
        for args in ({}, {"text": 3}, {"text": "", "path": "x"}, {"text": "x", "extra": "x"}):
            with self.assertRaises(ValueError):
                self.service.call("analyze_log", args)
        report_id = self.service.call("analyze_log", {"text": "hello"})["report_id"]
        for count in (-1, True, 101):
            with self.assertRaises(ValueError):
                self.service.call("get_log_excerpt", {"report_id": report_id, "start_line": 1, "count": count})

    def test_report_eviction(self):
        ids = [self.service.call("analyze_log", {"text": str(i)})["report_id"] for i in range(5)]
        with self.assertRaises(ValueError):
            self.service.call("get_report", {"report_id": ids[0], "section": "summary"})
        self.assertEqual(len(self.service.reports), 4)

    def test_catalog_and_map(self):
        catalog = self.service.call("list_log_formats", {})
        self.assertGreaterEqual(len(catalog["families"]), 30)
        self.assertIn("source_refs", self.service.call("list_log_formats", {"family": catalog["families"][0]["id"]}))
        path = self.root / "firmware.map"
        path.write_text("10000000 T probe\n")
        report_id = self.service.call("analyze_log", {"text": "PC: 10000005\n"})["report_id"]
        result = self.service.call("symbolize_with_map", {"report_id": report_id, "map_path": str(path)})
        self.assertFalse(result["build_identity_verified"])


class ProtocolTests(unittest.TestCase):
    def test_subprocess_lifecycle_and_tool_discovery(self):
        requests = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
            {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "analyze_log", "arguments": {"text": FAULT}}},
            {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "analyze_log", "arguments": {}}},
        ]
        child = subprocess.run([sys.executable, str(ROOT / "server.py")], input="\n".join(map(json.dumps, requests)) + "\n", text=True, capture_output=True, timeout=10)
        self.assertEqual(child.returncode, 0, child.stderr)
        self.assertEqual(child.stderr, "")
        messages = [json.loads(line) for line in child.stdout.splitlines()]
        self.assertEqual(len(messages), 4)
        self.assertEqual(len(messages[1]["result"]["tools"]), 5)
        self.assertEqual(messages[2]["result"]["structuredContent"]["summary"]["incident_count"], 1)
        self.assertTrue(messages[3]["result"]["isError"])

    def test_protocol_errors_and_version_negotiation(self):
        protocol = Protocol(DebugService())
        self.assertEqual(protocol.dispatch([])["error"]["code"], -32600)
        self.assertEqual(protocol.dispatch({"jsonrpc": "2.0", "method": "tools/list", "id": 1})["error"]["code"], -32000)
        init = protocol.dispatch({"jsonrpc": "2.0", "method": "initialize", "id": 2, "params": {"protocolVersion": "future"}})
        self.assertEqual(init["result"]["protocolVersion"], "2025-06-18")
        self.assertIsNone(protocol.dispatch({"jsonrpc": "2.0", "method": "notifications/initialized"}))
        self.assertEqual(protocol.dispatch({"jsonrpc": "2.0", "method": "missing", "id": 3})["error"]["code"], -32601)


if __name__ == "__main__":
    unittest.main()
