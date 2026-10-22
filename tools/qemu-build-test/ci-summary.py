#!/usr/bin/env python3
"""Reject incomplete CI runs and mismatched build/runtime evidence."""
import argparse
import json
from pathlib import Path
import sys


PROFILES = {
    'qemu/kernel_test': {
        'status': 'pass', 'suites': [('kernel_tc', 'Kernel', 432, 0)], 'extra': []},
    'qemu/build_test': {
        'status': 'pass_with_known_driver_failures',
        'suites': [('network_tc', 'Network', 163, 0), ('libcxx_utc', 'libc++', 795, 0),
                   ('filesystem_tc', 'Filesystem', 203, 0), ('kernel_tc', 'Kernel', 432, 0),
                   ('drivers_tc', 'Drivers', 14, 8)],
        'extra': ['network-peer', 'cpp', 'smartfs']},
}


def summarize(build, run):
    configuration = build.get('configuration')
    if configuration not in PROFILES or run.get('configuration') != configuration:
        raise ValueError('Build and runtime configurations differ or are unsupported')
    profile = PROFILES[configuration]
    status = profile['status']
    if build.get('status') != 'pass' or run.get('status') != status or run.get('container_exit') != 0:
        raise ValueError(run.get('error') or 'The clean build and selected run must complete')
    if run.get('boot_count') != 1 or run.get('kernel_omitted') is not False:
        raise ValueError('One boot including the kernel is required')
    if run.get('rounds_requested') != 3 or len(run.get('rounds', [])) != 3:
        raise ValueError('All three rounds are required')
    if build['source_commit'] != run['source_commit']:
        raise ValueError('Build and runtime source commits differ')
    if build['sha256']['firmware/tinyara'] != run['firmware_sha256']:
        raise ValueError('Build and runtime firmware hashes differ')
    if not (build['sha256']['effective.config'] == run['effective_config_sha256'] == run['defconfig_sha256']):
        raise ValueError('The effective configuration differs from the selected defconfig')
    if run['memory_growth_after_warmup'] > 0:
        raise ValueError('Heap usage increased after warmup')
    if profile['extra'] and not run.get('storage_guard_survived'):
        raise ValueError('Storage sentinel check failed')
    rows = []
    for number, round_result in enumerate(run['rounds'], 1):
        if round_result['number'] != number or round_result['status'] != status:
            raise ValueError('A round is incomplete or out of order')
        steps = {step['name']: step for step in round_result['steps']}
        expected = {item[0] for item in profile['suites']} | set(profile['extra'])
        if len(round_result['steps']) != len(expected) or set(steps) != expected:
            raise ValueError('A workload is missing or duplicated')
        row = [str(number)]
        for name, title, passed, failed in profile['suites']:
            step = steps[name]
            expected_status = 'known_failures' if failed else 'pass'
            if step['status'] != expected_status or (step['pass'], step['fail']) != (passed, failed):
                raise ValueError('Unexpected testcase result for ' + name)
            row.append('{0} / {1}'.format(passed, failed))
        if any(steps[name]['status'] != 'pass' for name in profile['extra']):
            raise ValueError('A smoke workload did not complete')
        row.extend(str(round_result['after'][name]) for name in ('used', 'task_count'))
        rows.append(row)
    return {'status': status, 'configuration': configuration, 'source_commit': build['source_commit'],
            'image_id': build['image_id'], 'firmware_sha256': run['firmware_sha256'], 'rows': rows,
            'columns': ['Round'] + [item[1] for item in profile['suites']] + ['Heap used', 'Tasks'],
            'elapsed_wall_seconds': run['elapsed_wall_seconds'], 'heap_growth_after_warmup': run['memory_growth_after_warmup']}


def markdown(report):
    text = ['## QEMU validation', '', 'Result: **' + report['status'] + '**', '']
    if report['status'] == 'fail':
        text.extend(['Validation did not complete. See build.log and runtime/runner.log.',
                     '', 'Error: ' + str(report['error']).replace('\n', ' '), ''])
    else:
        text.extend(['Configuration: `' + report['configuration'] + '`', '',
                     'One boot, three rounds. Values are PASS / FAIL.', '',
                     '| ' + ' | '.join(report['columns']) + ' |',
                     '| ' + ' | '.join('---' for _ in report['columns']) + ' |'])
        text.extend('| ' + ' | '.join(row) + ' |' for row in report['rows'])
        if report['status'] == 'pass_with_known_driver_failures':
            text.extend(['', 'Driver FAIL values are the exact unsupported-device baseline; they are not zero failures.',
                         'Each round also completed network peers, C++ smoke and 100 SmartFS fill/delete loops.'])
        text.extend(['', 'Elapsed: {0}s. Heap growth after warmup: {1} bytes.'.format(
            report['elapsed_wall_seconds'], report['heap_growth_after_warmup']), '',
            'Commit: `' + report['source_commit'] + '`', 'Image: `' + report['image_id'] + '`',
            'ELF SHA-256: `' + report['firmware_sha256'] + '`', ''])
    return '\n'.join(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        build = json.loads((args.output / 'build.json').read_text())
        run = json.loads((args.output / 'runtime/result.json').read_text())
        report = summarize(build, run)
    except (OSError, ValueError, KeyError, TypeError) as error:
        report = {'status': 'fail', 'error': str(error)}
    (args.output / 'ci-summary.json').write_text(json.dumps(report, indent=2) + '\n')
    text = markdown(report)
    (args.output / 'summary.md').write_text(text)
    print(text)
    return 1 if report['status'] == 'fail' else 0


if __name__ == '__main__':
    sys.exit(main())
