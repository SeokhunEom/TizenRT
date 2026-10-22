"""Reject stale or incomplete app lifecycle evidence; does not emulate firmware."""
import importlib.util
import itertools
from pathlib import Path
import sys
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location("mm_lifecycle_runner", SCRIPTS / "qemu-armv8m-mm-lifecycle.py")
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)

MAIN = b"NXMM APP2 MAIN PASS pid=12 live_bytes=257 normal_exit=1\r\n"
CHILD = b"NXMM APP2 PASS pid=15 after_main=1 final_child=1\r\n"
GROUP = b"NXMM APP2 GROUP PASS pid=16 after_main=1 after_child=1 separate_group=1\r\n"
CLEAN = b"NXMM LIFE CYCLE PASS round=1 main_pid=12 child_pid=15 group_pid=16 heap=0x80401230 ownership=1 init=1 destroy=1 registry=0 common=0 delayed=0 heap_restored=1\r\n"
START = b"NXMM LIFE ROUND BEGIN round=1\r\n"


class LifecycleEvidenceTest(unittest.TestCase):
    def test_boot_waits_for_all_events_in_any_order_and_advances_cursor(self):
        events = (b"TASH>>\0", b"binary_manager_load_binary: Load success! [Name: app1] [Version: 260611]\r\n", GROUP)
        for order in itertools.permutations(events):
            with self.subTest(order=order):
                partial = b"boot\r\n" + order[0] + order[1]
                self.assertIsNone(RUNNER.BOOT_READY.search(partial))
                transcript = partial + order[2] + b"\0"
                ready = RUNNER.BOOT_READY.search(transcript)
                self.assertIsNotNone(ready)
                self.assertEqual(ready.end(), len(transcript))
                # The old prompt must not satisfy a future shell response.
                self.assertNotIn(b"TASH>>", transcript[ready.end():])

    def test_incomplete_loader_or_group_line_does_not_release_boot_wait(self):
        loader = b"binary_manager_load_binary: Load success! [Name: app1] [Version: 260611]\r\n"
        for transcript in (b"TASH>>" + GROUP + loader[:-1], b"TASH>>" + loader + GROUP[:-1],
                           b"TASH>>" + loader.replace(b"app1", b"app2") + GROUP):
            with self.subTest(transcript=transcript):
                self.assertIsNone(RUNNER.BOOT_READY.search(transcript))

    def test_warmup_is_excluded_only_before_round_boundary(self):
        warmup = CHILD.replace(b"pid=15", b"pid=10") + GROUP.replace(b"pid=16", b"pid=9")
        result = RUNNER.validate_round(warmup + START + MAIN + CHILD + GROUP + CLEAN, 1)
        self.assertEqual(result["main_pid"], 12)
        with self.assertRaises(ValueError):
            RUNNER.validate_round(START + warmup + MAIN + CHILD + GROUP + CLEAN, 1)
        with self.assertRaises(ValueError):
            RUNNER.validate_round(b"NXMM APP2 FAIL\n" + START + MAIN + CHILD + GROUP + CLEAN, 1)

    def test_missing_wrong_or_duplicate_round_boundary_fails(self):
        for start in (b"", START.replace(b"round=1", b"round=2"), START + START):
            with self.subTest(start=start), self.assertRaises(ValueError):
                RUNNER.validate_round(start + MAIN + CHILD + GROUP + CLEAN, 1)

    def test_complete_ordered_round(self):
        result = RUNNER.validate_cycle(MAIN + CHILD + GROUP + CLEAN, 1)
        self.assertEqual(result["main_pid"], 12)
        self.assertEqual(result["child_pid"], 15)
        self.assertEqual(result["group_pid"], 16)
        self.assertTrue(result["kernel_heap_restored"])
        self.assertTrue(result["live_allocation_in_app_heap"])

    def test_previous_round_is_not_reused(self):
        with self.assertRaises(ValueError):
            RUNNER.validate_cycle(MAIN + CHILD + GROUP + CLEAN, 2)

    def test_missing_main_child_or_group_fails(self):
        for transcript in (MAIN + GROUP + CLEAN, CHILD + GROUP + CLEAN, MAIN + CHILD + CLEAN, CLEAN):
            with self.subTest(transcript=transcript), self.assertRaises(ValueError):
                RUNNER.validate_cycle(transcript, 1)

    def test_duplicate_evidence_fails(self):
        for transcript in (MAIN + MAIN + CHILD + GROUP + CLEAN, MAIN + CHILD + CHILD + GROUP + CLEAN,
                           MAIN + CHILD + GROUP + GROUP + CLEAN, MAIN + CHILD + GROUP + CLEAN + CLEAN):
            with self.subTest(transcript=transcript), self.assertRaises(ValueError):
                RUNNER.validate_cycle(transcript, 1)

    def test_mismatched_pid_or_order_fails(self):
        for transcript in (CHILD + MAIN + GROUP + CLEAN, MAIN + GROUP + CHILD + CLEAN,
                           MAIN + CHILD + CLEAN + GROUP, MAIN + CHILD.replace(b"pid=15", b"pid=16") + GROUP + CLEAN,
                           MAIN + CHILD + GROUP.replace(b"pid=16", b"pid=17") + CLEAN):
            with self.subTest(transcript=transcript), self.assertRaises(ValueError):
                RUNNER.validate_cycle(transcript, 1)

    def test_missing_cleanup_contract_fails(self):
        for old, new in ((b"ownership=1", b"ownership=0"), (b"destroy=1", b"destroy=0"), (b"registry=0", b"registry=1"),
                         (b"common=0", b"common=1"), (b"delayed=0", b"delayed=1"),
                         (b"heap_restored=1", b"heap_restored=0")):
            with self.subTest(field=old), self.assertRaises(ValueError):
                RUNNER.validate_cycle(MAIN + CHILD + GROUP + CLEAN.replace(old, new), 1)

    def test_failure_and_panic_cannot_be_hidden_by_success(self):
        for failure in (b"NXMM APP2 FAIL\n", b"NXMM LIFE CYCLE FAIL\n", b"PANIC\n", b"HardFault\n"):
            with self.subTest(failure=failure), self.assertRaises(ValueError):
                RUNNER.validate_cycle(failure + MAIN + CHILD + GROUP + CLEAN, 1)


if __name__ == "__main__":
    unittest.main()
