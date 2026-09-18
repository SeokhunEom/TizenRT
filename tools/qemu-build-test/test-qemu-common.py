#!/usr/bin/env python3
"""Regression checks for shared failure handling and workload verdicts."""
import contextlib
import io
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import qemu_common as common


class CommonTests(unittest.TestCase):
    def test_storage_fail_line_is_detected_with_lf_and_crlf(self):
        for ending in ('\n', '\r\n'):
            with self.subTest(ending=repr(ending)):
                qemu = Mock()
                qemu.shell.return_value = ending.join(('before', 'Fail', 'after'))
                with self.assertRaises(RuntimeError):
                    common.storage_command(qemu, 'smart_test')
        qemu.shell.return_value = 'Pass\r\n'
        self.assertEqual(common.storage_command(qemu, 'smart_test'), 'Pass\r\n')

    def test_smartfs_requires_complete_populated_fill_delete_cycles(self):
        seek = 'Pass\nPass\n'
        complete = ''.join('=== FILLING {0} x\nNumber of files: 3\n=== DELETING {0} x\nNumber of files: 2\n'.format(i)
                           for i in (1, 2))
        for stress, accepted in [(complete, True), (complete.replace('DELETING 2', 'DELETING 1'), False),
                                 (complete.replace('files: 3', 'files: 0'), False)]:
            with self.subTest(accepted=accepted, stress=stress):
                command = Mock(side_effect=[seek, '', stress])
                if accepted:
                    self.assertEqual(common.check_smartfs(command, 2, '/mnt/probe')['loops'], 2)
                else:
                    with self.assertRaises(RuntimeError):
                        common.check_smartfs(command, 2, '/mnt/probe')

    def test_smartfs_requires_both_seek_write_and_circular_log_passes(self):
        with self.assertRaises(RuntimeError):
            common.check_smartfs(Mock(return_value='Pass\n'), 2, '/mnt/probe')

    def test_helloxx_keeps_language_version_and_constructor_checks(self):
        output = ['dynamically constructed instance\n', 'constructed on the stack\n',
                  'statically constructed instance\nc++ version used : 201703\n']
        qemu = Mock()
        qemu.wait.side_effect = output
        self.assertEqual(common.check_helloxx(qemu, 201703)['status'], 'pass')
        qemu.wait.side_effect = output
        with self.assertRaises(RuntimeError):
            common.check_helloxx(qemu, 201103)
        qemu.wait.side_effect = [output[0], '', output[2]]
        with self.assertRaises(RuntimeError):
            common.check_helloxx(qemu, 201703)

    def test_fault_wins_over_a_ready_prompt(self):
        with tempfile.TemporaryDirectory() as directory:
            qemu = common.QemuSession(Path(directory))
            try:
                for fault in (b'up_hardfault:', b'BusFault', b'CONSTRUCTION FAILED', b'NETPEER FAIL'):
                    qemu.data = bytearray(fault + b'\nTASH>>')
                    with self.subTest(fault=fault), self.assertRaises(RuntimeError):
                        qemu.wait(b'TASH>>')
            finally:
                qemu.close()

    def test_missing_marker_times_out(self):
        with tempfile.TemporaryDirectory() as directory:
            qemu = common.QemuSession(Path(directory))
            try:
                with self.assertRaises(TimeoutError):
                    qemu.wait(b'TASH>>', timeout=0)
            finally:
                qemu.close()

    def test_cleanup_failure_cannot_leave_a_pass_verdict(self):
        with tempfile.TemporaryDirectory() as directory:
            qemu = common.QemuSession(Path(directory))
            result = {'status': 'pass'}
            with patch.object(qemu, 'close', side_effect=OSError('cleanup failed')):
                with contextlib.redirect_stdout(io.StringIO()):
                    qemu.finish(result)
            qemu.close()
            saved = json.loads((Path(directory) / 'result.json').read_text())
            self.assertEqual(saved['status'], 'fail')
            self.assertEqual(saved['cleanup_error'], 'cleanup failed')

    def test_old_evidence_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'evidence'
            out.mkdir()
            (out / 'serial.log').write_text('previous evidence')
            with patch.object(common.subprocess, 'call') as run:
                with self.assertRaises(ValueError):
                    common.run_container(Path(directory), out, 'image', 'boot-smoke.py', [], 60)
                run.assert_not_called()
            self.assertEqual((out / 'serial.log').read_text(), 'previous evidence')

    def test_standalone_diagnostics_include_output_drained_at_exit(self):
        spec = importlib.util.spec_from_file_location('run_testcases', str(Path(__file__).with_name('run-testcases.py')))
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            qemu = Mock()
            qemu.data = bytearray()
            qemu.start.side_effect = TimeoutError('boot timed out')

            def finish(result):
                qemu.data.extend(b'[last] FAIL\n')
                common.save_result(output, result)

            qemu.finish.side_effect = finish
            args = SimpleNamespace(output=output, timeout=1, suite='kernel_tc', check_storage=False)
            with patch.object(runner, 'QemuSession', return_value=qemu):
                self.assertEqual(runner.inside(args), 1)
            saved = json.loads((output / 'result.json').read_text())
            self.assertEqual(saved['observed_fail_lines'], 1)
            self.assertEqual(saved['failure_lines'], ['[last] FAIL'])

    def test_missing_docker_still_produces_failed_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('build/output/bin/tinyara', 'os/.config', 'build/configs/qemu/build_test/defconfig'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('fixture')
            out = root / 'new' / 'evidence'
            with patch.object(common.subprocess, 'call', side_effect=OSError('docker missing')):
                with patch.object(common.subprocess, 'check_output', return_value=b'commit\n'):
                    with contextlib.redirect_stdout(io.StringIO()):
                        code = common.run_container(root, out, 'image', 'boot-smoke.py', [], 60)
            self.assertEqual(code, 1)
            saved = json.loads((out / 'result.json').read_text())
            self.assertEqual(saved['status'], 'fail')
            self.assertIn('docker missing', saved['runner_error'])


if __name__ == '__main__':
    unittest.main()
