#!/usr/bin/env python3
"""Summarize a complete CI run without promoting partial runs to success."""
import argparse
import json
from pathlib import Path
import sys

STATUS = 'pass_with_known_driver_failures'
SUITES = ('network_tc', 'libcxx_utc', 'filesystem_tc', 'kernel_tc', 'drivers_tc')


def summarize(build, run):
    if build.get('status') != 'pass':
        raise ValueError('The clean build did not complete')
    if run.get('status') != STATUS or run.get('container_exit') != 0:
        raise ValueError(run.get('error') or 'The full-set run did not complete successfully')
    if run.get('boot_count') != 1 or run.get('kernel_omitted') is not False:
        raise ValueError('A full run must use one boot and include the kernel')
    if run.get('rounds_requested') != 3 or len(run.get('rounds', [])) != 3:
        raise ValueError('All three rounds are required')
    if build['source_commit'] != run['source_commit']:
        raise ValueError('Build and runtime source commits differ')
    if build['sha256']['firmware/tinyara'] != run['firmware_sha256']:
        raise ValueError('Build and runtime firmware hashes differ')
    if not (build['sha256']['effective.config'] == run['effective_config_sha256'] == run['defconfig_sha256']):
        raise ValueError('The effective configuration differs from the build_test defconfig')
    if run['memory_growth_after_warmup'] > 0 or not run['storage_guard_survived']:
        raise ValueError('Resource stability or storage sentinel check failed')
    rows = []
    for number, round_result in enumerate(run['rounds'], 1):
        if round_result['number'] != number or round_result['status'] != STATUS:
            raise ValueError('A round is incomplete or out of order')
        steps = {step['name']: step for step in round_result['steps']}
        if len(round_result['steps']) != 8 or set(steps) != set(SUITES) | {'network-peer', 'cpp', 'smartfs'}:
            raise ValueError('A workload is missing or duplicated')
        for name, step in steps.items():
            expected = 'known_failures' if name == 'drivers_tc' else 'pass'
            if step['status'] != expected:
                raise ValueError('A workload did not complete: ' + name)
        row = [str(number)]
        row.extend('{0} / {1}'.format(steps[name]['pass'], steps[name]['fail']) for name in SUITES)
        row.extend(str(round_result['after'][name]) for name in ('used', 'task_count'))
        rows.append(row)
    return {'status': STATUS, 'source_commit': build['source_commit'], 'image_id': build['image_id'],
            'firmware_sha256': run['firmware_sha256'], 'rows': rows,
            'elapsed_wall_seconds': run['elapsed_wall_seconds'],
            'heap_growth_after_warmup': run['memory_growth_after_warmup']}


def markdown(report):
    text = ['## QEMU build_test', '', 'Result: **' + report['status'] + '**', '']
    if report['status'] == 'fail':
        text.extend(['Validation did not complete. See build.log, image-build.log and full-set/runner.log.',
                     '', 'Error: ' + str(report['error']).replace('\n', ' '), ''])
    else:
        text.extend(['One boot, three full rounds. Values below are PASS / FAIL.', '',
                     '| Round | Network | libc++ | Filesystem | Kernel | Drivers | Heap used | Tasks |',
                     '|---|---|---|---|---|---|---|---|'])
        text.extend('| ' + ' | '.join(row) + ' |' for row in report['rows'])
        text.extend(['', 'Driver FAIL values are the exact reviewed unsupported-device baseline; they are not zero failures.',
                     'Each round also completed C++ smoke, network peers and 100 SmartFS fill/delete loops.', '',
                     'Elapsed: {0}s. Heap growth after warmup: {1} bytes.'.format(
                         report['elapsed_wall_seconds'], report['heap_growth_after_warmup']), '',
                     'Commit: `' + report['source_commit'] + '`',
                     'Image: `' + report['image_id'] + '`',
                     'ELF SHA-256: `' + report['firmware_sha256'] + '`', ''])
    return '\n'.join(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        build = json.loads((args.output / 'build.json').read_text())
        run = json.loads((args.output / 'full-set/result.json').read_text())
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
