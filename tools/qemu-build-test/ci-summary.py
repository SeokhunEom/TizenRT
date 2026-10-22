#!/usr/bin/env python3
"""Reject incomplete CI runs and mismatched build/runtime evidence."""
import argparse
import json
from pathlib import Path
import sys


def summarize(build, run):
    if build.get('status') != 'pass' or run.get('status') != 'pass' or run.get('container_exit') != 0:
        raise ValueError(run.get('error') or 'The clean build and kernel run must complete')
    if build.get('configuration') != 'qemu/kernel_test' or run.get('configuration') != build['configuration']:
        raise ValueError('Build and runtime configurations differ')
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
    rows = []
    for number, round_result in enumerate(run['rounds'], 1):
        if round_result['number'] != number or round_result['status'] != 'pass':
            raise ValueError('A round is incomplete or out of order')
        steps = round_result['steps']
        if len(steps) != 1 or steps[0]['name'] != 'kernel_tc':
            raise ValueError('The kernel workload is missing or duplicated')
        step = steps[0]
        if step['status'] != 'pass' or (step['pass'], step['fail']) != (432, 0):
            raise ValueError('The kernel workload did not pass completely')
        rows.append([str(number), '{0} / {1}'.format(step['pass'], step['fail']),
                     str(round_result['after']['used']), str(round_result['after']['task_count'])])
    return {'status': 'pass', 'configuration': build['configuration'], 'source_commit': build['source_commit'],
            'image_id': build['image_id'], 'firmware_sha256': run['firmware_sha256'], 'rows': rows,
            'elapsed_wall_seconds': run['elapsed_wall_seconds'], 'heap_growth_after_warmup': run['memory_growth_after_warmup']}


def markdown(report):
    text = ['## QEMU kernel tests', '', 'Result: **' + report['status'] + '**', '']
    if report['status'] == 'fail':
        text.extend(['Validation did not complete. See build.log and runtime/runner.log.',
                     '', 'Error: ' + str(report['error']).replace('\n', ' '), ''])
    else:
        text.extend(['One boot, three kernel rounds. Values are PASS / FAIL.', '',
                     '| Round | Kernel | Heap used | Tasks |', '|---|---|---|---|'])
        text.extend('| ' + ' | '.join(row) + ' |' for row in report['rows'])
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
