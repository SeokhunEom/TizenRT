#!/usr/bin/env python3
"""Check CI failure propagation and build/runtime evidence matching."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('ci_summary', str(HERE / 'ci-summary.py'))
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


def evidence():
    build = {'status': 'pass', 'source_commit': 'commit', 'image_id': 'sha256:image',
             'sha256': {'firmware/tinyara': 'elf', 'effective.config': 'config'}}
    run = {'status': summary.STATUS, 'container_exit': 0, 'boot_count': 1,
           'kernel_omitted': False, 'rounds_requested': 3, 'source_commit': 'commit',
           'firmware_sha256': 'elf', 'effective_config_sha256': 'config',
           'defconfig_sha256': 'config', 'memory_growth_after_warmup': -16,
           'storage_guard_survived': True, 'elapsed_wall_seconds': 2200, 'rounds': []}
    counts = {'network_tc': (163, 0), 'libcxx_utc': (795, 0),
              'filesystem_tc': (203, 0), 'kernel_tc': (433, 0), 'drivers_tc': (14, 8)}
    for number in range(1, 4):
        steps = [{'name': name, 'status': 'known_failures' if fail else 'pass',
                  'pass': passed, 'fail': fail} for name, (passed, fail) in counts.items()]
        steps.extend({'name': name, 'status': 'pass'} for name in ('network-peer', 'cpp', 'smartfs'))
        run['rounds'].append({'number': number, 'status': summary.STATUS, 'steps': steps,
                              'after': {'used': 77856, 'task_count': 8}})
    return build, run


class SummaryTests(unittest.TestCase):
    def test_complete_run_keeps_known_driver_failures_visible(self):
        report = summary.summarize(*evidence())
        self.assertEqual(report['status'], summary.STATUS)
        self.assertEqual(report['rows'][0][5], '14 / 8')
        self.assertIn('not zero failures', summary.markdown(report))

    def test_partial_or_failed_run_cannot_pass(self):
        for key, value in [('status', 'diagnostic_pass'), ('status', 'running'),
                           ('container_exit', 1), ('kernel_omitted', True),
                           ('boot_count', 2), ('rounds_requested', 2)]:
            with self.subTest(key=key, value=value):
                build, run = evidence()
                run[key] = value
                with self.assertRaises(ValueError):
                    summary.summarize(build, run)

    def test_missing_round_or_workload_cannot_pass(self):
        for missing in ('round', 'workload'):
            with self.subTest(missing=missing):
                build, run = evidence()
                if missing == 'round':
                    run['rounds'].pop()
                else:
                    run['rounds'][1]['steps'].pop()
                with self.assertRaises(ValueError):
                    summary.summarize(build, run)

    def test_build_and_runtime_must_match(self):
        for key in ('source_commit', 'firmware_sha256', 'effective_config_sha256', 'defconfig_sha256'):
            with self.subTest(key=key):
                build, run = evidence()
                run[key] = 'different'
                with self.assertRaises(ValueError):
                    summary.summarize(build, run)

    def test_resource_regression_cannot_pass(self):
        for key, value in [('memory_growth_after_warmup', 16), ('storage_guard_survived', False)]:
            with self.subTest(key=key):
                build, run = evidence()
                run[key] = value
                with self.assertRaises(ValueError):
                    summary.summarize(build, run)

    def test_missing_runtime_artifact_produces_failed_summary_and_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / 'build.json').write_text(json.dumps(evidence()[0]))
            process = subprocess.run([sys.executable, str(HERE / 'ci-summary.py'), '--output', directory],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(process.returncode, 1)
            self.assertEqual(json.loads((out / 'ci-summary.json').read_text())['status'], 'fail')
            self.assertIn('did not complete', (out / 'summary.md').read_text())

    def test_complete_artifacts_produce_successful_summary_and_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / 'full-set').mkdir()
            build, run = evidence()
            (out / 'build.json').write_text(json.dumps(build))
            (out / 'full-set/result.json').write_text(json.dumps(run))
            process = subprocess.run([sys.executable, str(HERE / 'ci-summary.py'), '--output', directory],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(process.returncode, 0, process.stderr.decode())
            self.assertEqual(json.loads((out / 'ci-summary.json').read_text())['status'], summary.STATUS)


if __name__ == '__main__':
    unittest.main()
