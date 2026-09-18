#!/usr/bin/env python3
"""Repeat the complete build_test workload without rebooting QEMU."""
import argparse
from collections import Counter
import importlib.util
import json
from pathlib import Path
import re
import sys
import time

from qemu_common import QemuSession, run_container, save_result, check_helloxx, check_cxxtest, storage_command, check_smartfs

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('qemu_network', str(HERE / 'network-smoke.py'))
network = importlib.util.module_from_spec(spec)
spec.loader.exec_module(network)

SUITES = {'network_tc': ('Network TC', 163, 0), 'libcxx_utc': ('Libc++ TC', 795, 0),
          'filesystem_tc': ('FileSystem TC', 203, 0), 'drivers_tc': ('Drivers TC', 14, 8),
          'kernel_tc': ('Kernel TC', 433, 0)}
TRANSIENTS = set(SUITES) | {'helloxx', 'cxxtest', 'smart', 'smart_test', 'network_peer', 'ping'}


def parse_suite(output, suite, policy):
    label, expected_pass, expected_fail = SUITES[suite]
    totals = re.findall(re.escape(label) + r' End \[PASS\s*:\s*(\d+), FAIL\s*:\s*(\d+)\]', output)
    if len(totals) != 1:
        raise RuntimeError('Missing or duplicate completion for ' + suite)
    passed, failed = map(int, totals[0])
    passes = re.findall(r'(?m)^\[[^\r\n]+\] PASS\s*$', output)
    failures = [line for line in output.splitlines() if re.search(r'\] FAIL\b|TC Assertion FAIL', line)]
    normalized = [re.sub(r'\[Line : \d+\] ', '', line) for line in failures]
    if (passed, failed) != (expected_pass, expected_fail) or len(passes) != passed or len(failures) != failed:
        raise RuntimeError('Unexpected or inconsistent counts for ' + suite + ': ' + repr((passed, failed)))
    if suite == 'drivers_tc':
        if Counter(normalized) != Counter(policy['failure_signatures']):
            raise RuntimeError('Driver failures differ from the reviewed unsupported-device baseline')
    elif failures:
        raise RuntimeError('Unexpected testcase failure in ' + suite)
    return {'suite': suite, 'status': 'known_failures' if failed else 'pass',
            'pass': passed, 'fail': failed, 'failure_lines': failures}


def snapshot(qemu, expected_ids=None):
    deadline = time.monotonic() + 15
    while True:
        ps = qemu.shell('ps', ['PID | PRIO'])
        names = [line.rsplit('|', 1)[1].strip() for line in ps.splitlines()
                 if re.match(r'^\s*\d+\s*\|', line)]
        if not names:
            raise RuntimeError('Empty task inventory')
        ids = [int(line.split('|', 1)[0]) for line in ps.splitlines() if re.match(r'^\s*\d+\s*\|', line)]
        if not TRANSIENTS.intersection(names) and (expected_ids is None or sorted(ids) == sorted(expected_ids)):
            break
        if time.monotonic() >= deadline:
            raise RuntimeError('Test tasks remained after completion: ' + repr(names))
        time.sleep(0.05)
    free = qemu.shell('free')
    match = re.search(r'Mem:\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', free)
    if not match:
        raise RuntimeError('Missing heap counters')
    total, used, available, largest = map(int, match.groups())
    if total != used + available or largest > available:
        raise RuntimeError('Inconsistent heap counters')
    return {'tasks': names, 'task_ids': ids, 'task_count': len(names), 'total': total, 'used': used,
            'free': available, 'largest': largest, 'ps_output': ps, 'free_output': free}


def check_storage(qemu):
    content = qemu.shell('cat /mnt/full-set-guard')
    if 'QEMU-full-set-guard' not in content.splitlines():
        raise RuntimeError('Storage sentinel changed')
    mounts = qemu.shell('mount')
    actual_mounts = set(re.findall(r'(?m)^\s*(/\S*) type (\S+)\s*$', mounts))
    if actual_mounts != {('/mnt', 'smartfs'), ('/proc', 'procfs'), ('/tmp', 'tmpfs')}:
        raise RuntimeError('Required mount disappeared')
    return mounts



def storage_snapshot(qemu):
    mounts = check_storage(qemu)
    raw_df = qemu.shell('df')
    volumes = {}
    for size, blocks, used, available, mount in re.findall(r'(?m)^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(/\S*)\s*$', raw_df):
        volumes[mount] = dict(zip(('block_size', 'blocks', 'used', 'available'), map(int, (size, blocks, used, available))))
    if set(volumes) != {'/mnt', '/proc', '/tmp'}:
        raise RuntimeError('Incomplete filesystem capacity report')
    files = {}
    raw_listings = {}
    for mount in ('/mnt', '/tmp'):
        listing = qemu.shell('ls -Rs ' + mount)
        raw_listings[mount] = listing
        current = None
        entries = []
        for line in listing.splitlines():
            line = line.strip().replace('\x00', '')
            if line.startswith('/') and line.endswith(':'):
                current = line[:-1]
            elif line and line != 'TASH>>':
                if current is None or not re.match(r'^\d+\s+.+', line):
                    raise RuntimeError('Invalid recursive file inventory: ' + line)
                entries.append(current + '/' + line)
        files[mount] = sorted(entries)
    return {'mounts': mounts, 'volumes': volumes, 'files': files, 'df_output': raw_df, 'ls_output': raw_listings}


def verify_storage_stability(warm, final):
    if warm['files'] != final['files'] or warm['volumes'] != final['volumes']:
        raise RuntimeError('Filesystem files or capacity changed after warmup')


def run_suite(qemu, suite, policy, timeout):
    if suite == 'drivers_tc':
        nodes = qemu.shell('ls /dev')
        for node in policy['absent_devices']:
            if re.search(r'(?m)^\s*' + re.escape(node) + r'\s*$', nodes):
                raise RuntimeError('Unsupported device unexpectedly exists: ' + node)
    label = SUITES[suite][0]
    started = time.monotonic()
    qemu.send(suite)
    output = qemu.wait(re.escape(label.encode()) + rb' Start', 30)
    output += qemu.wait(re.escape(label.encode()) + rb' End \[PASS\s*:\s*\d+, FAIL\s*:\s*\d+\]', timeout)
    report = parse_suite(output, suite, policy)
    report['elapsed_seconds'] = round(time.monotonic() - started, 3)
    return report


def cpp(qemu):
    check_helloxx(qemu)
    snapshot(qemu)
    check_cxxtest(qemu)
    return {'status': 'pass', 'commands': ['helloxx', 'cxxtest']}


def storage(qemu):
    def command(cmd, markers=()):
        # The 100-cycle stress can exceed the default 90 seconds on CI runners.
        return storage_command(qemu, cmd, markers, timeout=300 if cmd == 'smart' else None)
    command('umount /mnt')
    if '/mnt type smartfs' in command('mount'):
        raise RuntimeError('/mnt remained mounted')
    command('mount -t smartfs /dev/smart0 /mnt')
    check_storage(qemu)
    return check_smartfs(command, 100, '/mnt/full-set')


def inside(args):
    out = args.output
    policy = json.loads((HERE / 'known-driver-failures.json').read_text())
    result = {'status': 'running', 'boot_count': 0, 'rounds_requested': args.rounds,
              'kernel_omitted': args.omit_kernel, 'rounds': [], 'driver_policy': policy}
    qemu = QemuSession(out)
    try:
        qemu.start(network=True)
        result['boot_count'] += 1
        result['qemu_command'] = qemu.command
        commands = qemu.shell('help')
        if any(name not in commands for name in TRANSIENTS - {'ping'}):
            raise RuntimeError('Incomplete full-set command registry')
        qemu.shell('echo QEMU-full-set-guard > /mnt/full-set-guard')
        check_storage(qemu)
        result['initial'] = snapshot(qemu)
        result['storage_initial'] = storage_snapshot(qemu)
        for number in range(1, args.rounds + 1):
            current = {'number': number, 'steps': [], 'status': 'running'}
            result['rounds'].append(current)
            workloads = [('network-peer', lambda: network_round(qemu)),
                         ('network_tc', lambda: run_suite(qemu, 'network_tc', policy, 120)),
                         ('cpp', lambda: cpp(qemu)),
                         ('libcxx_utc', lambda: run_suite(qemu, 'libcxx_utc', policy, 180)),
                         ('filesystem_tc', lambda: run_suite(qemu, 'filesystem_tc', policy, 300)),
                         ('smartfs', lambda: storage(qemu)),
                         ('drivers_tc', lambda: run_suite(qemu, 'drivers_tc', policy, 120))]
            if not args.omit_kernel:
                workloads.append(('kernel_tc', lambda: run_suite(qemu, 'kernel_tc', policy, args.kernel_timeout)))
            for name, work in workloads:
                result['active_step'] = {'round': number, 'name': name}
                save_result(out, result)
                print('Round {0}: {1}'.format(number, name), flush=True)
                step = work()
                step['name'] = name
                current['steps'].append(step)
                step['after'] = snapshot(qemu, result['initial']['task_ids'])
                check_storage(qemu)
                if Counter(step['after']['task_ids']) != Counter(result['initial']['task_ids']):
                    raise RuntimeError('Background task set changed after ' + name)
                save_result(out, result)
            current['after'] = snapshot(qemu, result['initial']['task_ids'])
            current['storage_after'] = storage_snapshot(qemu)
            current['status'] = 'pass_with_known_driver_failures'
            save_result(out, result)
        if not args.omit_kernel and args.rounds >= 3:
            warm = result['rounds'][1]['after']
            final = result['rounds'][-1]['after']
            result['memory_growth_after_warmup'] = final['used'] - warm['used']
            verify_storage_stability(result['rounds'][1]['storage_after'], result['rounds'][-1]['storage_after'])
            if final['used'] > warm['used']:
                raise RuntimeError('Heap usage kept growing after the second full round')
        result['final'] = snapshot(qemu)
        result['mounts'] = check_storage(qemu)
        qemu.shell('rm /mnt/full-set-guard')
        result['storage_guard_survived'] = True
        result['status'] = 'diagnostic_pass' if args.omit_kernel or args.rounds < 3 else 'pass_with_known_driver_failures'
        result.pop('active_step', None)
    except Exception as exc:
        result['status'] = 'fail'
        result['error'] = str(exc)
    finally:
        qemu.finish(result)
    return 1 if result['status'] == 'fail' else 0


def network_round(qemu):
    report = {'status': 'pass', 'checks': []}
    qemu.shell('ifdown eth0', ['OK'])
    network.check_network(qemu, report, cycles=1)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--image', default='tizenrt/qemu-build-test:2.12.0-16m')
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--kernel-timeout', type=float, default=1500)
    parser.add_argument('--omit-kernel', action='store_true', help='Diagnostic run only; cannot produce full-set PASS')
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.rounds <= 0 or args.kernel_timeout <= 0:
        parser.error('Round count and timeout must be positive')
    if args.inside:
        return inside(args)
    if args.root is None:
        parser.error('--root is required')
    extra = ['--rounds', str(args.rounds), '--kernel-timeout', str(args.kernel_timeout)]
    if args.omit_kernel:
        extra.append('--omit-kernel')
    timeout = args.rounds * ((0 if args.omit_kernel else args.kernel_timeout) + 1200) + 120
    return run_container(args.root, args.output, args.image, 'full-set.py', extra, timeout)


if __name__ == '__main__':
    sys.exit(main())
