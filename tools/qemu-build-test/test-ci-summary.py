#!/usr/bin/env python3
"""Validate CI completion and firmware provenance requirements."""
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
    build = {'status': 'pass', 'configuration': 'qemu/kernel_test', 'source_commit': 'commit',
             'image_id': 'sha256:image', 'sha256': {'firmware/tinyara': 'elf', 'effective.config': 'config'}}
    run = {'status': 'pass', 'configuration': 'qemu/kernel_test', 'container_exit': 0, 'boot_count': 1,
           'kernel_omitted': False, 'rounds_requested': 3, 'source_commit': 'commit',
           'firmware_sha256': 'elf', 'effective_config_sha256': 'config', 'defconfig_sha256': 'config',
           'memory_growth_after_warmup': 0, 'elapsed_wall_seconds': 400, 'rounds': []}
    for number in range(1, 4):
        run['rounds'].append({'number': number, 'status': 'pass',
                              'steps': [{'name': 'kernel_tc', 'status': 'pass', 'pass': 432, 'fail': 0}],
                              'after': {'used': 23000, 'task_count': 6}})
    return build, run


class SummaryTests(unittest.TestCase):
    def test_complete_kernel_run_passes(self):
        report = summary.summarize(*evidence())
        self.assertEqual(report['rows'][0][1], '432 / 0')

    def test_partial_failed_or_wrong_configuration_cannot_pass(self):
        for key, value in [('status', 'diagnostic_pass'), ('container_exit', 1), ('kernel_omitted', True),
                           ('boot_count', 2), ('rounds_requested', 2), ('configuration', 'qemu/build_test')]:
            with self.subTest(key=key):
                build, run = evidence()
                run[key] = value
                with self.assertRaises(ValueError):
                    summary.summarize(build, run)

    def test_missing_round_workload_or_testcase_cannot_pass(self):
        for missing in ('round', 'workload', 'testcase'):
            with self.subTest(missing=missing):
                build, run = evidence()
                if missing == 'round':
                    run['rounds'].pop()
                elif missing == 'workload':
                    run['rounds'][1]['steps'].pop()
                else:
                    run['rounds'][1]['steps'][0]['pass'] -= 1
                with self.assertRaises(ValueError):
                    summary.summarize(build, run)

    def test_build_and_runtime_must_match(self):
        for key in ('source_commit', 'firmware_sha256', 'effective_config_sha256', 'defconfig_sha256'):
            with self.subTest(key=key):
                build, run = evidence()
                run[key] = 'different'
                with self.assertRaises(ValueError):
                    summary.summarize(build, run)

    def test_resource_growth_cannot_pass(self):
        build, run = evidence()
        run['memory_growth_after_warmup'] = 16
        with self.assertRaises(ValueError):
            summary.summarize(build, run)

    def test_cli_requires_runtime_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            build, run = evidence()
            (out / 'build.json').write_text(json.dumps(build))
            command = [sys.executable, str(HERE / 'ci-summary.py'), '--output', directory]
            process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(process.returncode, 1)
            self.assertEqual(json.loads((out / 'ci-summary.json').read_text())['status'], 'fail')
            (out / 'runtime').mkdir()
            (out / 'runtime/result.json').write_text(json.dumps(run))
            process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(process.returncode, 0, process.stderr.decode())
            self.assertEqual(json.loads((out / 'ci-summary.json').read_text())['status'], 'pass')


def full_evidence():
    build, run = evidence()
    build['configuration'] = run['configuration'] = 'qemu/build_test'
    run['status'] = 'pass_with_known_driver_failures'
    run['storage_guard_survived'] = True
    counts = [('network_tc', 163, 0), ('libcxx_utc', 795, 0), ('filesystem_tc', 203, 0),
              ('kernel_tc', 432, 0), ('drivers_tc', 14, 8)]
    for result in run['rounds']:
        result['status'] = run['status']
        result['steps'] = [{'name': name, 'status': 'known_failures' if failed else 'pass',
                            'pass': passed, 'fail': failed} for name, passed, failed in counts]
        result['steps'].extend({'name': name, 'status': 'pass'} for name in ('network-peer', 'cpp', 'smartfs'))
    return build, run


class FullSummaryTests(unittest.TestCase):
    def test_complete_full_run_retains_raw_driver_failures(self):
        report = summary.summarize(*full_evidence())
        self.assertEqual(report['status'], 'pass_with_known_driver_failures')
        self.assertEqual(report['rows'][0][5], '14 / 8')
        self.assertIn('not zero failures', summary.markdown(report))

    def test_full_resources_and_completion_are_required(self):
        for key, value in [('status', 'pass'), ('status', 'diagnostic_pass'),
                           ('storage_guard_survived', False), ('memory_growth_after_warmup', 16),
                           ('kernel_omitted', True), ('rounds_requested', 2)]:
            with self.subTest(key=key):
                build, run = full_evidence()
                run[key] = value
                with self.assertRaises(ValueError):
                    summary.summarize(build, run)

    def test_missing_duplicate_failed_or_changed_workload_is_rejected(self):
        for fault in ('missing', 'duplicate', 'smoke', 'counts'):
            with self.subTest(fault=fault):
                build, run = full_evidence()
                steps = run['rounds'][1]['steps']
                if fault == 'missing':
                    steps.pop()
                elif fault == 'duplicate':
                    steps[-1] = steps[-2]
                elif fault == 'smoke':
                    steps[-1]['status'] = 'fail'
                else:
                    steps[0]['pass'] -= 1
                with self.assertRaises(ValueError):
                    summary.summarize(build, run)


if __name__ == '__main__':
    unittest.main()
