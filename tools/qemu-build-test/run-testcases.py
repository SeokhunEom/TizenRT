#!/usr/bin/env python3
"""Run one existing build_test testcase in a fresh QEMU instance."""
import argparse
import re
import sys

from qemu_common import QemuSession, parse_smoke_args, run_container, save_result

SUITES = {'network_tc': 'Network TC', 'kernel_tc': 'Kernel TC', 'drivers_tc': 'Drivers TC',
          'filesystem_tc': 'FileSystem TC', 'libcxx_utc': 'Libc++ TC'}


def inside(args):
    result = {'suite': args.suite, 'status': 'fail', 'completed': False, 'pass': None, 'fail': None,
              'timeout_seconds': args.timeout}
    qemu = QemuSession(args.output, 30)
    try:
        qemu.start(timeout=60)
        result['inventory'] = {probe: qemu.shell(probe) for probe in ('help', 'ls /dev', 'mount', 'free')}
        if args.suite not in result['inventory']['help']:
            raise RuntimeError('Requested suite is absent from the TASH command registry')
        if args.check_storage:
            for device in ('smart0', 'smart1', 'mtdblock1'):
                if not re.search(r'(?m)^\s*' + device + r'\s*$', result['inventory']['ls /dev']):
                    raise RuntimeError('Missing storage fixture: ' + device)
            qemu.shell('echo QEMU-storage-guard > /mnt/qemu-storage-guard')
            if 'QEMU-storage-guard' not in qemu.shell('cat /mnt/qemu-storage-guard').splitlines():
                raise RuntimeError('Could not create storage guard')
            result['storage_guard_survived'] = False
        label = re.escape(SUITES[args.suite])
        qemu.send(args.suite)
        qemu.wait((label + ' Start').encode(), 30)
        pattern = label + r' End \[PASS\s*:\s*(\d+), FAIL\s*:\s*(\d+)\]'
        output = qemu.wait(pattern.encode(), args.timeout)
        result['completed'] = True
        result['pass'], result['fail'] = map(int, re.search(pattern, output).groups())
        # Standalone diagnostics keep their raw zero-failure verdict. The full
        # set separately enforces exact counts and known driver signatures.
        result['status'] = 'pass' if result['pass'] > 0 and result['fail'] == 0 else 'fail'
        qemu.shell('')
        result['post_suite'] = {probe: qemu.shell(probe) for probe in ('ps', 'free', 'mount', 'ls /dev')}
        if args.check_storage:
            after = qemu.shell('cat /mnt/qemu-storage-guard')
            result['storage_guard_output'] = after
            if 'QEMU-storage-guard' not in after.splitlines():
                raise RuntimeError('Suite changed /mnt backing storage or removed its guard file')
            result['storage_guard_survived'] = True
            qemu.shell('rm /mnt/qemu-storage-guard')
    except Exception as exc:
        result['status'] = 'fail'
        result['error'] = str(exc)
    finally:
        qemu.finish(result)
        # Include output drained while stopping QEMU in the final diagnostics.
        result['observed_pass_lines'] = len(re.findall(rb'\] PASS\s*(?:\r?\n|$)', qemu.data))
        result['observed_fail_lines'] = len(re.findall(rb'\] FAIL\b|TC Assertion FAIL', qemu.data))
        result['failure_lines'] = [line for line in qemu.data.decode('utf-8', 'replace').splitlines()
                                   if '[FAIL]' in line or 'TC Assertion FAIL' in line or re.search(r'\]\s+FAIL\b', line)]
        save_result(args.output, result)
    return 0 if result['status'] == 'pass' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=SUITES, required=True)
    parser.add_argument('--check-storage', action='store_true', help='Check /mnt contents survive the suite')
    args = parse_smoke_args(parser, 600)
    if args.inside:
        return inside(args)
    extra = ['--suite', args.suite, '--timeout', str(args.timeout)]
    if args.check_storage:
        extra.append('--check-storage')
    return run_container(args.root, args.output, args.image, 'run-testcases.py', extra, args.timeout + 600)


if __name__ == '__main__':
    sys.exit(main())
