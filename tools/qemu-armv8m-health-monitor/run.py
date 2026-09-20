#!/usr/bin/env python3
"""Health Monitor tests using the ARMv8-M port's authoritative QEMU layout."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import sys
import time

if sys.version_info < (3, 10):
    raise SystemExit('Python 3.10+ is required by the ARMv8-M port runner')

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'qemu-build-test'))

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, str(path))
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj

hm = module('hm_smoke', HERE.parent / 'qemu-build-test/health-monitor-smoke.py')
extended = module('hm_extended', HERE.parent / 'qemu-build-test/health-monitor-extended.py')

class Session(hm.HealthSession):
    def __init__(self, args):
        super().__init__(args.output)
        self.root = args.root
        self.timer_irq = 19
        self.kernel_passes = 459 if args.profile == 'hello' else 447
        self.args = args
        self.eof = False

    def start(self):
        sys.path.insert(0, str(self.root / '.github/scripts'))
        port = module('armv8_port', self.root / '.github/scripts/qemu-armv8m-kernel-tc.py')
        if self.args.profile == 'hello':
            self.command = port.qemu_command(self.root, self.args.profile)
        else:
            from qemu_armv8m_ab import stage_state, extract_active_kernel, qemu_command
            state = self.output / 'qemu.state'
            stage_state(self.root, self.args.profile, state)
            kernel = extract_active_kernel(self.root, self.args.profile, state)
            self.command = port.with_user_nic(qemu_command(state, kernel))
        if self.args.case == 'reset' and '-no-reboot' not in self.command:
            self.command.append('-no-reboot')
        self.process = subprocess.Popen(self.command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, bufsize=0)
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        boot = self.wait(rb'TASH>>', 60)
        self.observe(1)
        if b'QEMU health monitor: /dev/health_monitor ready' not in self.data:
            raise RuntimeError('Health Monitor device registration missing')
        self.shell('help', ['health_monitor'])
        return boot

    def pump(self, interval=0.2):
        for key, _ in self.selector.select(interval):
            chunk = os.read(key.fileobj.fileno(), 65536)
            if not chunk:
                self.eof = True
                self.selector.unregister(key.fileobj)
                continue
            self.data.extend(chunk)
            self.log.write(chunk)
            self.log.flush()
        self.check_faults()
        if self.eof and self.expected_assert is None:
            raise RuntimeError('QEMU output closed unexpectedly')

    def check_faults(self):
        super().check_faults()
        if re.search(rb'HM_(?:QEMU|USER|CONTROL) FAIL', self.data):
            raise RuntimeError('Health Monitor fixture failed')


def expiry(q, result, ms, reset=False):
    source = (q.root / 'os/kernel/health_monitor/health_monitor.c').read_text().splitlines()
    lines = [i + 1 for i, line in enumerate(source) if line.strip() == 'PANIC();']
    if len(lines) != 1:
        raise RuntimeError('PANIC site ambiguous')
    q.expected_assert = re.compile(rb'Assertion failed at file:[^\r\n]*health_monitor/health_monitor\.c line:\s*' + str(lines[0]).encode() + rb'\b')
    q.send('health_monitor expire ' + str(ms))
    q.wait(rb'health_monitor: deliberate timeout; expect PANIC/reboot reason 62\r?\n')
    start = time.monotonic()
    q.wait(q.expected_assert, ms / 1000 + 10)
    elapsed = time.monotonic() - start
    q.wait(rb'Checking kernel heap for corruption', 10)
    q.observe(0.2)
    if not re.search(rb'IRQ num:\s*19\b', q.data):
        raise RuntimeError('Expected expiry from TIMER0 IRQ 19')
    if abs(elapsed - ms / 1000) > max(0.15, ms / 1000 * 0.05):
        raise RuntimeError('Deadline wall-time mismatch: ' + str(elapsed))
    if reset:
        q.process.wait(timeout=10)
        if q.process.returncode != 0:
            raise RuntimeError('Software reset did not exit QEMU cleanly')
    result.update(timeout_ms=ms, observed_seconds=round(elapsed, 3), timer_irq=19,
                  exact_health_panic=True, software_reset_observed=reset,
                  physical_reset_verified=False, reboot_reason_persistence_verified=False)


def control(q, command):
    q.send('hm_armv8 ' + command)
    output = q.wait(rb'HM_CONTROL (?:STATUS|SENT|RECOVER dispatched) [^\r\n]*\r?\n', 30)
    q.wait_for_exit('hm_armv8')
    return output


def user_ready(q, apps):
    for app in apps:
        pattern = ('HM_USER READY app=' + app + r' pid=\d+').encode()
        # Boot output may precede the first TASH prompt; inspect all collected data.
        deadline = time.monotonic() + 60
        while not re.search(pattern, q.data):
            if time.monotonic() > deadline:
                raise RuntimeError('User app not ready: ' + app)
            q.pump()
        if not re.search(('HM_USER BOOT app=' + app + r' pid=\d+ control=\d+ unprivileged=1').encode(), q.data):
            raise RuntimeError('User app did not execute unprivileged: ' + app)


def empty_status(q):
    out = control(q, 'status')
    if not re.search(r'HM_CONTROL STATUS count=0 hint=0', out):
        raise RuntimeError('Registration or heap hint remains: ' + out)
    return out


def protected(q, args, result):
    apps = ('app1',) if args.profile == 'xip_all' else ('app1', 'app2')
    user_ready(q, apps)
    result['baseline'] = hm.snapshot(q)
    result['initial_empty'] = empty_status(q)
    result['rounds'] = []
    for number in range(args.rounds):
        record = {'number': number + 1, 'apps': []}
        result['rounds'].append(record)
        for app in apps:
            q.send('hm_armv8 send ' + app + ' api')
            out = q.wait(('HM_USER PASS app=' + app + r' checks=\d+ pid=\d+ cycles=1000[^\r\n]*').encode(), 60)
            q.wait_for_exit('hm_armv8')
            record['apps'].append(out)
        record['empty'] = empty_status(q)
        record['snapshot'] = hm.snapshot(q)
        if record['snapshot']['task_ids'] != result['baseline']['task_ids']:
            raise RuntimeError('Protected API test leaked a task')
    result['heap_growth_after_warmup'] = result['rounds'][-1]['snapshot']['used'] - result['rounds'][0]['snapshot']['used']
    if result['heap_growth_after_warmup'] > 0:
        raise RuntimeError('Protected app tests grew kernel heap')
    if args.case == 'user-api':
        return
    if args.case == 'user-expire':
        source = (q.root / 'os/kernel/health_monitor/health_monitor.c').read_text().splitlines()
        line = next(i + 1 for i, text in enumerate(source) if text.strip() == 'PANIC();')
        q.expected_assert = re.compile(rb'Assertion failed at file:[^\r\n]*health_monitor/health_monitor\.c line:\s*' + str(line).encode() + rb'\b')
        q.send('hm_armv8 send app1 expire')
        notice = q.wait(rb'HM_USER EXPIRE app=app1 pid=\d+ timeout_ms=1000')
        start = time.monotonic()
        q.wait(q.expected_assert, 10)
        elapsed = time.monotonic() - start
        q.observe(0.2)
        if not re.search(rb'IRQ num:\s*19\b', q.data) or abs(elapsed - 1) > 0.15:
            raise RuntimeError('Wrong user deadline/IRQ')
        result.update(user_expiry_notice=notice, observed_seconds=round(elapsed, 3), kernel_panic=True)
        return
    if args.case in ('user-return', 'user-return-empty', 'user-return-app2'):
        app = 'app2' if args.case == 'user-return-app2' else 'app1'
        command = 'return-empty' if args.case == 'user-return-empty' else 'return'
        q.send('hm_armv8 send ' + app + ' ' + command)
        result['return_notice'] = q.wait(('HM_USER RETURN app=' + app + r' pid=\d+ registered=' + ('0' if command == 'return-empty' else '1')).encode())
        q.observe(0.8)
        result['after_return'] = empty_status(q)
        result['snapshot_after_return'] = hm.snapshot(q)
        old_pid = int(re.search(r'pid=(\d+)', result['return_notice']).group(1))
        if old_pid in result['snapshot_after_return']['task_ids']:
            raise RuntimeError('Registered user main did not exit')
        return
    if args.case not in ('user-reload', 'user-reload-empty'):
        raise RuntimeError('Unknown protected case')
    result['reloads'] = []
    if args.case == 'user-reload-empty':
        result['baseline_heap_detail'] = q.shell('heapinfo -k -a', timeout=30)
    for number in range(args.rounds):
        record = {'number': number + 1}
        result['reloads'].append(record)
        if args.case == 'user-reload':
            q.send('hm_armv8 send app1 arm')
            armed = q.wait(rb'HM_USER ARMED app=app1 pid=\d+ children=4 timeout_ms=5000', 20)
            q.wait_for_exit('hm_armv8')
            record['armed'] = armed
            record['before'] = control(q, 'status')
            if 'count=5 hint=1' not in record['before']:
                raise RuntimeError('Expected five real registered user threads')
            old_pids = set(map(int, re.findall(r'HM_CONTROL TASK pid=(\d+)', record['before'])))
        else:
            record['before'] = empty_status(q)
            old_pids = {int(re.findall(('HM_USER READY app=' + app + r' pid=(\d+)').encode(), q.data)[-1]) for app in apps}
        start = len(q.data)
        q.send('hm_armv8 recover')
        q.wait(rb'HM_CONTROL RECOVER dispatched=1', 30)
        deadline = time.monotonic() + 60
        while any(not re.search(('HM_USER READY app=' + app + r' pid=\d+').encode(), q.data[start:]) for app in apps):
            if time.monotonic() > deadline:
                raise RuntimeError('Reloaded apps failed readiness')
            q.pump()
        q.observe(5.2)
        record['empty'] = empty_status(q)
        record['snapshot'] = hm.snapshot(q)
        if old_pids.intersection(record['snapshot']['task_ids']):
            raise RuntimeError('Old monitored task survived reload')
        if record['snapshot']['task_count'] != result['baseline']['task_count']:
            raise RuntimeError('Task count changed after reload')
        if args.case == 'user-reload-empty':
            record['heap_detail'] = q.shell('heapinfo -k -a', timeout=30)
        record['old_pids'] = sorted(old_pids)
        record['reload_log'] = bytes(q.data[start:]).decode('utf-8', 'replace')
        if b'QEMU health monitor: /dev/health_monitor ready' in q.data[start:]:
            raise RuntimeError('System rebooted instead of unloading apps')
    result['reload_heap_growth_after_warmup'] = result['reloads'][-1]['snapshot']['used'] - result['reloads'][0]['snapshot']['used']
    if result['reload_heap_growth_after_warmup'] > 0:
        raise RuntimeError('Kernel heap grew over repeated reloads')


def flat(q, args, result):
    if args.case in ('expiry', 'reset'):
        return expiry(q, result, args.ms, args.case == 'reset')
    if args.case == 'normal':
        return hm.normal(q, result)
    if args.case == 'basic':
        result['snapshot'] = hm.snapshot(q)
        result['checks'] = [hm.command(q, 'health_monitor run 1000 50 10', 'health_monitor: run completed'),
                            hm.command(q, 'health_monitor stop 100', 'health_monitor: stop completed'),
                            hm.command(q, 'health_monitor exit 1000 10', 'health_monitor: exit completed')]
        return
    if args.case.startswith('fatal-'):
        return extended.fatal(q, args.case[6:], result)
    cases = extended.NORMAL + ('many',) if args.case == 'all' else (args.case,)
    result['baseline'] = extended.snapshot(q)
    result['rounds'] = []
    for number in range(args.rounds):
        record = {'number': number + 1, 'cases': []}
        result['rounds'].append(record)
        for case in cases:
            record['cases'].append(extended.run_case(q, case))
        record['snapshot'] = extended.snapshot(q)
        if record['snapshot']['task_ids'] != result['baseline']['task_ids']:
            raise RuntimeError('Fixture leaked a task')
    result['heap_growth_after_warmup'] = result['rounds'][-1]['snapshot']['used'] - result['rounds'][0]['snapshot']['used']
    if result['heap_growth_after_warmup'] > 0:
        raise RuntimeError('Heap grows between completed rounds')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--profile', choices=('hello', 'loadable_all', 'loadable_apps', 'xip_all'), required=True)
    p.add_argument('--case', required=True)
    p.add_argument('--rounds', type=int, default=3)
    p.add_argument('--ms', type=int, default=1000)
    args = p.parse_args()
    args.root = args.root.resolve(); args.output = args.output.resolve()
    if args.rounds < 1 or args.ms < 1:
        p.error('rounds and ms must be positive')
    args.output.mkdir(parents=True, exist_ok=False)
    result = {'status': 'fail', 'profile': args.profile, 'case': args.case,
              'source_head': subprocess.check_output(['git', '-C', str(args.root), 'rev-parse', 'HEAD']).decode().strip(),
              'firmware_sha256': hashlib.sha256((args.root / 'build/output/bin/tinyara').read_bytes()).hexdigest(),
              'effective_config_sha256': hashlib.sha256((args.root / 'os/.config').read_bytes()).hexdigest(),
              'qemu_version': subprocess.check_output(['qemu-system-arm', '--version']).decode().splitlines()[0]}
    q = Session(args)
    try:
        q.start()
        (protected if args.case.startswith('user-') else flat)(q, args, result)
        result['status'] = 'pass'
    except Exception as exc:
        result['error'] = str(exc)
    finally:
        q.finish(result)
        try:
            q.check_faults()
        except Exception as exc:
            result.update(status='fail', error=str(exc))
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return int(result['status'] != 'pass')

if __name__ == '__main__':
    sys.exit(main())
