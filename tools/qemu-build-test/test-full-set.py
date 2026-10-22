#!/usr/bin/env python3
"""Checks that the integration verdict rejects missing or changed evidence."""
import importlib.util
import json
import io
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import qemu_common

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('full_set', str(HERE / 'full-set.py'))
full = importlib.util.module_from_spec(spec)
spec.loader.exec_module(full)
POLICY = json.loads((HERE / 'known-driver-failures.json').read_text())


def drivers():
    passes = ''.join('[supported_{0}] PASS\n'.format(i) for i in range(14))
    failures = '\n'.join(line.replace(' FAIL ', ' FAIL [Line : 42] ', 1) for line in POLICY['failure_signatures'])
    return passes + failures + '\nDrivers TC End [PASS : 14, FAIL : 8]'


class VerdictTests(unittest.TestCase):
    def storage_with_delayed_progress(self, elapsed, complete):
        stress = ''.join('=== FILLING {0} x\nNumber of files: 3\n=== DELETING {0} x\nNumber of files: 2\n'.format(i)
                         for i in range(1, 101))
        if complete:
            stress += 'Final memory usage:\nTASH>>'
        clock = [0.0]
        chunks = iter([b'Files are still being written\n', stress.encode()])
        times = iter([95.0, elapsed])
        with tempfile.TemporaryDirectory() as directory:
            qemu = qemu_common.QemuSession(Path(directory))
            qemu.data.extend(b'smart\r\n')
            qemu.process = SimpleNamespace(stdin=io.BytesIO(), poll=lambda: None)
            original_shell = qemu.shell
            mounts = iter(['TASH>>', '/mnt type smartfs\n/proc type procfs\n/tmp type tmpfs\nTASH>>'])

            def shell(command, required=(), **kwargs):
                if command == 'smart':
                    return original_shell(command, required, **kwargs)
                if command == 'mount':
                    return next(mounts)
                if command == 'cat /mnt/full-set-guard':
                    return 'QEMU-full-set-guard\nTASH>>'
                if command.startswith('smart_test '):
                    return 'Pass\nPass\n'
                return 'TASH>>'

            def progress(unused_timeout):
                clock[0] = next(times)
                return [(SimpleNamespace(fileobj=SimpleNamespace(fileno=lambda: 123)), 1)]

            try:
                with patch.object(qemu, 'shell', side_effect=shell), \
                     patch.object(qemu.selector, 'select', side_effect=progress), \
                     patch.object(qemu_common.os, 'read', side_effect=lambda fd, size: next(chunks)), \
                     patch.object(qemu_common.time, 'monotonic', side_effect=lambda: clock[0]):
                    return full.storage(qemu)
            finally:
                qemu.process = None
                qemu.close()

    def test_smart_stress_can_complete_after_default_shell_timeout(self):
        result = self.storage_with_delayed_progress(190, True)
        self.assertEqual((result['status'], result['loops']), ('pass', 100))

    def test_smart_stress_still_times_out_with_continuing_output(self):
        with self.assertRaises(TimeoutError):
            self.storage_with_delayed_progress(301, False)

    def test_exact_known_failure_baseline_is_reported_as_failures(self):
        result = full.parse_suite(drivers(), 'drivers_tc', POLICY)
        self.assertEqual(result['status'], 'known_failures')
        self.assertEqual((result['pass'], result['fail']), (14, 8))

    def test_same_count_with_new_failure_is_rejected(self):
        changed = drivers().replace('tc_driver_adc_read', 'unexpected_test')
        with self.assertRaises(RuntimeError):
            full.parse_suite(changed, 'drivers_tc', POLICY)

    def test_same_case_different_failure_reason_is_rejected(self):
        changed = drivers().replace('adc_open', 'adc_read')
        with self.assertRaises(RuntimeError):
            full.parse_suite(changed, 'drivers_tc', POLICY)

    def test_duplicate_known_failure_cannot_hide_missing_case(self):
        changed = drivers().replace('tc_driver_adc_read', 'tc_driver_adc_open_close')
        with self.assertRaises(RuntimeError):
            full.parse_suite(changed, 'drivers_tc', POLICY)

    def test_complete_network_report_passes(self):
        text = ''.join('[test_{0}] PASS\n'.format(i) for i in range(163))
        text += 'Network TC End [PASS : 163, FAIL : 0]'
        self.assertEqual(full.parse_suite(text, 'network_tc', POLICY)['status'], 'pass')

    def test_storage_growth_is_rejected(self):
        warm = {'files': {'/mnt': ['guard']}, 'volumes': {'/mnt': {'available': 100}}}
        final = {'files': {'/mnt': ['guard']}, 'volumes': {'/mnt': {'available': 99}}}
        with self.assertRaises(RuntimeError):
            full.verify_storage_stability(warm, final)

    def test_extra_file_is_rejected_even_at_same_capacity(self):
        warm = {'files': {'/mnt': ['guard']}, 'volumes': {}}
        final = {'files': {'/mnt': ['guard', 'leaked']}, 'volumes': {}}
        with self.assertRaises(RuntimeError):
            full.verify_storage_stability(warm, final)

    def test_completion_summary_alone_does_not_pass(self):
        with self.assertRaises(RuntimeError):
            full.parse_suite('Network TC End [PASS : 163, FAIL : 0]', 'network_tc', POLICY)

    def test_missing_completion_does_not_pass(self):
        with self.assertRaises(RuntimeError):
            full.parse_suite(drivers().split('Drivers TC End')[0], 'drivers_tc', POLICY)

    def test_duplicate_completion_does_not_pass(self):
        with self.assertRaises(RuntimeError):
            full.parse_suite(drivers() + '\nDrivers TC End [PASS : 14, FAIL : 8]', 'drivers_tc', POLICY)

    def test_failure_hidden_by_zero_fail_summary_is_rejected(self):
        text = ''.join('[test_{0}] PASS\n'.format(i) for i in range(163))
        text += '[hidden] FAIL\nNetwork TC End [PASS : 163, FAIL : 0]'
        with self.assertRaises(RuntimeError):
            full.parse_suite(text, 'network_tc', POLICY)


if __name__ == '__main__':
    unittest.main()
