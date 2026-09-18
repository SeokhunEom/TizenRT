#!/usr/bin/env python3
"""Verify C++ constructors and the existing RTTI/exception/STL/thread example in QEMU."""
import argparse
from pathlib import Path
import sys

from qemu_common import QemuSession, parse_smoke_args, run_container, check_helloxx, check_cxxtest


def inside(args):
    result = {'status': 'fail', 'checks': [], 'timeout_seconds': args.timeout}
    qemu = QemuSession(args.output, args.timeout)
    try:
        config = Path('/work/os/.config').read_text()
        boot = qemu.start()
        result['checks'].append({'name': 'boot', 'status': 'pass', 'output': boot})
        commands = qemu.shell('help')
        result['inventory'] = {'help': commands, 'mount': qemu.shell('mount')}
        required = ('helloxx',) if args.basic_only else ('helloxx', 'cxxtest')
        if any(name not in commands for name in required):
            raise RuntimeError('Required C++ command is absent from TASH')
        version = 201703 if 'CONFIG_CXX_VERSION_17=y' in config else 201402 if 'CONFIG_CXX_VERSION_14=y' in config else 201103
        result['checks'].append(check_helloxx(qemu, version))
        if not args.basic_only:
            result['checks'].append(check_cxxtest(qemu))
        result['post_free'] = qemu.shell('free', ['Mem:'])
        result['status'] = 'pass'
    except Exception as exc:
        result['error'] = str(exc)
    finally:
        qemu.finish(result)
    return 0 if result['status'] == 'pass' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--basic-only', action='store_true', help='Run helloxx without cxxtest')
    args = parse_smoke_args(parser, 60)
    if args.inside:
        return inside(args)
    extra = ['--timeout', str(args.timeout)]
    if args.basic_only:
        extra.append('--basic-only')
    return run_container(args.root, args.output, args.image, 'cpp-smoke.py', extra, args.timeout * 20 + 60)


if __name__ == '__main__':
    sys.exit(main())
