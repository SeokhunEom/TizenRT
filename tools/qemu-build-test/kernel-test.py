#!/usr/bin/env python3
"""Run the kernel-only QEMU configuration three times without rebooting."""
import argparse
from pathlib import Path
import re
import sys

from qemu_common import QemuSession, run_container, save_result, snapshot

EXPECTED_PASS = 432


def parse_kernel(output):
    totals = re.findall(r'Kernel TC End \[PASS\s*:\s*(\d+), FAIL\s*:\s*(\d+)\]', output)
    passes = re.findall(r'(?m)^\[[^\r\n]+\] PASS\s*$', output)
    failures = re.findall(r'\] FAIL\b|TC Assertion FAIL', output)
    if len(totals) != 1 or tuple(map(int, totals[0])) != (EXPECTED_PASS, 0):
        raise RuntimeError('Missing, duplicate or unexpected kernel completion')
    if len(passes) != EXPECTED_PASS or failures:
        raise RuntimeError('Kernel testcase output disagrees with its completion summary')
    return {'name': 'kernel_tc', 'status': 'pass', 'pass': EXPECTED_PASS, 'fail': 0}


def inside(args):
    result = {'status': 'running', 'boot_count': 0, 'rounds_requested': args.rounds,
              'kernel_omitted': False, 'rounds': []}
    qemu = QemuSession(args.output)
    try:
        qemu.start()
        result['boot_count'] = 1
        commands = qemu.shell('help')
        if 'kernel_tc' not in commands:
            raise RuntimeError('kernel_tc is absent from the command registry')
        result['initial'] = snapshot(qemu)
        for number in range(1, args.rounds + 1):
            result['active_step'] = {'round': number, 'name': 'kernel_tc'}
            save_result(args.output, result)
            print('Round {0}: kernel_tc'.format(number), flush=True)
            qemu.send('kernel_tc')
            qemu.wait(rb'Kernel TC Start', 30)
            output = qemu.wait(rb'Kernel TC End \[PASS\s*:\s*\d+, FAIL\s*:\s*\d+\]', args.timeout)
            step = parse_kernel(output)
            after = snapshot(qemu, result['initial']['task_ids'])
            step['after'] = after
            result['rounds'].append({'number': number, 'status': 'pass', 'steps': [step], 'after': after})
            save_result(args.output, result)
        if args.rounds >= 3:
            result['memory_growth_after_warmup'] = result['rounds'][-1]['after']['used'] - result['rounds'][1]['after']['used']
            if result['memory_growth_after_warmup'] > 0:
                raise RuntimeError('Kernel heap usage grew after the second round')
        result['status'] = 'pass' if args.rounds >= 3 else 'diagnostic_pass'
        result.pop('active_step', None)
    except Exception as exc:
        result['status'] = 'fail'
        result['error'] = str(exc)
    finally:
        qemu.finish(result)
    return 1 if result['status'] == 'fail' else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--image', default='tizenrt/qemu-build-test-ci:2.12.0-gcc10.3')
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--timeout', type=float, default=600)
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.rounds <= 0 or args.timeout <= 0:
        parser.error('Round count and timeout must be positive')
    if args.inside:
        return inside(args)
    if args.root is None:
        parser.error('--root is required')
    extra = ['--rounds', str(args.rounds), '--timeout', str(args.timeout)]
    return run_container(args.root, args.output, args.image, 'kernel-test.py', extra,
                         args.rounds * (args.timeout + 120) + 120, configuration='qemu/kernel_test')


if __name__ == '__main__':
    sys.exit(main())
