#!/usr/bin/env python3
"""Verify RAM-backed SmartFS commands, stress loops, and remount data."""
import argparse
from pathlib import Path
import re
import sys

from qemu_common import QemuSession, parse_smoke_args, run_container, storage_command, check_smartfs


def inside(args):
    result = {'status': 'fail', 'checks': [], 'timeout_seconds': args.timeout}
    qemu = QemuSession(args.output, args.timeout)

    def command(cmd, required=()):
        output = storage_command(qemu, cmd, required)
        result['checks'].append({'name': cmd, 'status': 'pass', 'output': output})
        return output

    try:
        config = Path('/work/os/.config').read_text()
        match = re.search(r'^CONFIG_EXAMPLES_SMART_NLOOPS=(\d+)$', config, re.M)
        if not match or int(match.group(1)) <= 0:
            raise ValueError('storage-smoke requires a finite CONFIG_EXAMPLES_SMART_NLOOPS')
        loops = int(match.group(1))
        result['expected_smart_loops'] = loops
        boot = qemu.start()
        if 'QEMU storage: /mnt ready' not in boot:
            raise RuntimeError('Storage initialization did not complete')
        result['checks'].append({'name': 'boot', 'status': 'pass', 'output': boot})
        command('ls /dev', ('smart0', 'smart1', 'mtdblock1'))
        command('mount', ('/mnt type smartfs',))
        command('echo QEMU-remount-data > /mnt/remount.txt')
        if 'QEMU-remount-data' not in command('cat /mnt/remount.txt').splitlines():
            raise RuntimeError('File contents did not match after write')
        command('umount /mnt')
        if '/mnt type smartfs' in command('mount'):
            raise RuntimeError('Unmount did not remove /mnt')
        command('mount -t smartfs /dev/smart0 /mnt')
        if 'QEMU-remount-data' not in command('cat /mnt/remount.txt').splitlines():
            raise RuntimeError('File contents did not survive remount')
        report = check_smartfs(command, loops, '/mnt/stage03')
        result['smart_loops'] = report['loops']
        result['minimum_file_count'] = report['minimum_file_count']
        if 'QEMU-remount-data' not in command('cat /mnt/remount.txt').splitlines():
            raise RuntimeError('Unrelated file was damaged by stress')
        command('rm /mnt/remount.txt')
        command('ls /mnt')
        command('free', ('Mem:',))
        result['status'] = 'pass'
    except Exception as exc:
        result['error'] = str(exc)
    finally:
        qemu.finish(result)
    return 0 if result['status'] == 'pass' else 1


def main():
    args = parse_smoke_args(argparse.ArgumentParser(description=__doc__), 300)
    if args.inside:
        return inside(args)
    return run_container(args.root, args.output, args.image, 'storage-smoke.py',
                         ['--timeout', str(args.timeout)], args.timeout * 20 + 120)


if __name__ == '__main__':
    sys.exit(main())
