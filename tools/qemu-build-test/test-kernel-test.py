#!/usr/bin/env python3
"""Exercise kernel verdicts against incomplete and inconsistent serial logs."""
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('kernel_test', str(HERE / 'kernel-test.py'))
kernel = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kernel)


def serial():
    return '\n'.join('[case_{0}] PASS'.format(i) for i in range(432)) + '\nKernel TC End [PASS : 432, FAIL : 0]'


class KernelVerdictTests(unittest.TestCase):
    def test_complete_kernel_log_passes(self):
        self.assertEqual(kernel.parse_kernel(serial())['pass'], 432)

    def test_summary_without_testcases_fails(self):
        with self.assertRaises(RuntimeError):
            kernel.parse_kernel('Kernel TC End [PASS : 432, FAIL : 0]')

    def test_hidden_failure_fails_even_with_zero_failure_summary(self):
        with self.assertRaises(RuntimeError):
            kernel.parse_kernel('[case] FAIL\n' + serial())

    def test_missing_duplicate_and_changed_completion_fail(self):
        for output in (serial().split('Kernel TC End')[0], serial() + '\n' + serial(),
                       serial().replace('PASS : 432', 'PASS : 431')):
            with self.subTest(output=output[-100:]), self.assertRaises(RuntimeError):
                kernel.parse_kernel(output)

    def test_extra_task_is_not_accepted_after_completion(self):
        class Session:
            def shell(self, command, required=()):
                return 'PID | PRIO\n 0 | idle\n 5 | tash\n 9 | kernel_tc\n'
        with patch('qemu_common.time.monotonic', side_effect=[0, 16]):
            with self.assertRaises(RuntimeError):
                kernel.snapshot(Session(), [0, 5])

    def test_fault_takes_precedence_over_prompt(self):
        with tempfile.TemporaryDirectory() as directory:
            session = kernel.QemuSession(Path(directory))
            session.data.extend(b'Assertion failed\nTASH>>')
            try:
                with self.assertRaises(RuntimeError):
                    session.wait(b'TASH>>')
            finally:
                session.close()

    def test_final_drain_cannot_hide_faults_or_an_unexpected_exit(self):
        class Process:
            def __init__(self, code, output):
                self.returncode = code
                self.stdout = io.BytesIO(output)

            def poll(self):
                return self.returncode

            def terminate(self):
                self.returncode = 0

            def wait(self, timeout=None):
                return self.returncode

        for code, output, expected in [(1, b'', 'fail'), (0, b'', 'fail'),
                                      (None, b'PANIC\n', 'fail'), (None, b'', 'pass')]:
            with self.subTest(code=code, output=output), tempfile.TemporaryDirectory() as directory:
                session = kernel.QemuSession(Path(directory))
                session.process = Process(code, output)
                result = {'status': 'pass'}
                with patch('sys.stdout', io.StringIO()):
                    session.finish(result)
                self.assertEqual(result['status'], expected)


if __name__ == '__main__':
    unittest.main()
