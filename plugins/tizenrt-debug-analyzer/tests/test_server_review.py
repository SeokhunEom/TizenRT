"""Integration regressions found during review of the MCP debugging workflow."""
import json
import unittest

from tizenrt_debug.server import DebugService, MAX_RESPONSE_BYTES, Protocol


class ServerReviewTests(unittest.TestCase):
    def test_every_advertised_report_section_can_be_read(self):
        service = DebugService()
        result = service.call("analyze_log", {"text": (
            "sysdbg_print: Displaying the SEMAPHORE HISTORY for 4 count\n"
            "sysdbg_print: 12345 WT worker 80001000 80002000 12\n"
        )})
        self.assertIn("history", result["sections"])
        for section in result["sections"]:
            with self.subTest(section=section):
                page = service.call("get_report", {
                    "report_id": result["report_id"], "section": section,
                    "offset": 0, "limit": 2,
                })
                self.assertEqual(section, page["section"])
        history = service.call("get_report", {
            "report_id": result["report_id"], "section": "history",
        })
        self.assertGreater(history["total"], 0)

    def test_oversized_record_does_not_hide_following_evidence(self):
        service = DebugService()
        result = service.call("analyze_log", {
            "text": "unrecognized-" + "x" * MAX_RESPONSE_BYTES + "\nimportant-tail-evidence\n",
        })
        first = service.call("get_report", {
            "report_id": result["report_id"], "section": "unknown_lines",
            "offset": 0, "limit": 2,
        })
        self.assertEqual(2, first["total"])
        self.assertIsNotNone(first["next_offset"], "An oversized record must leave a cursor to later records")
        next_page = service.call("get_report", {
            "report_id": result["report_id"], "section": "unknown_lines",
            "offset": first["next_offset"], "limit": 2,
        })
        self.assertIn("important-tail-evidence", json.dumps(next_page))

    def test_repeated_suite_footers_do_not_expand_initial_or_summary_response_unboundedly(self):
        service = DebugService()
        result = service.call("analyze_log", {
            "text": "########## KERNEL TC End [PASS : 23, FAIL : 0] ##########\n" * 8000,
        })
        self.assertLessEqual(len(json.dumps(result, ensure_ascii=False).encode()), MAX_RESPONSE_BYTES)
        summary = service.call("get_report", {
            "report_id": result["report_id"], "section": "summary",
        })
        self.assertLessEqual(len(json.dumps(summary, ensure_ascii=False).encode()), MAX_RESPONSE_BYTES)
        # Bounded summaries must leave the actual per-suite evidence queryable.
        tests = service.call("get_report", {
            "report_id": result["report_id"], "section": "tests", "offset": 7999,
        })
        self.assertEqual(8000, tests["total"])
        self.assertEqual(1, len(tests["items"]))

    def test_unknown_tool_is_a_protocol_error_not_a_failed_tool_execution(self):
        # MCP 2025-06-18/server/tools#error-handling distinguishes these channels.
        protocol = Protocol(DebugService())
        protocol.dispatch({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "protocolVersion": "2025-06-18", "capabilities": {},
            "clientInfo": {"name": "review", "version": "1"},
        }})
        protocol.dispatch({"jsonrpc": "2.0", "method": "notifications/initialized"})
        result = protocol.dispatch({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
                                    "params": {"name": "unknown_debug_tool"}})
        self.assertEqual(-32602, result.get("error", {}).get("code"))
        self.assertNotIn("result", result)


if __name__ == "__main__":
    unittest.main()
