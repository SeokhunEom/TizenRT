#!/usr/bin/env python3
"""Check a fresh LM3S6965 QEMU boot and three TASH commands."""
import argparse
import sys

from qemu_common import QemuSession, parse_smoke_args, run_container


def inside(args):
    result = {'status': 'fail', 'checks': [], 'timeout_seconds': args.timeout}
    qemu = QemuSession(args.output, args.timeout)
    try:
        boot = qemu.start()
        result['checks'].append({'name': 'boot', 'status': 'pass', 'output': boot})
        for command, required in [('help', ['kernel_tc', 'ps', 'free']),
                                  ('ps', ['PID', 'PRIO', 'tash']), ('free', ['total', 'Mem:'])]:
            output = qemu.shell(command)
            if any(word.lower() not in output.lower() for word in required):
                raise RuntimeError(command + ': missing output markers')
            result['checks'].append({'name': command, 'status': 'pass', 'output': output})
        result['status'] = 'pass'
    except Exception as exc:
        result['error'] = str(exc)
    finally:
        qemu.finish(result)
    return 0 if result['status'] == 'pass' else 1


def main():
    args = parse_smoke_args(argparse.ArgumentParser(description=__doc__), 60)
    if args.inside:
        return inside(args)
    return run_container(args.root, args.output, args.image, 'boot-smoke.py',
                         ['--timeout', str(args.timeout)], args.timeout * 8 + 30)


if __name__ == '__main__':
    sys.exit(main())
