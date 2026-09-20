#!/usr/bin/env python3
"""Validate the temporary Health Monitor integration on single-core QEMU."""
import argparse
import os
from pathlib import Path
import re
import sys
import time

from qemu_common import QemuSession, parse_smoke_args, run_container, save_result


class HealthSession(QemuSession):
    # Match actual faults, not the example's documented/intentional PANIC text.
    faults = re.compile(rb'Assertion failed|HardFault|BusFault|UsageFault|qemu: fatal|CONSTRUCTION FAILED', re.I)

    def __init__(self, output):
        super().__init__(output, 30)
        self.expected_assert = None

    def check_faults(self):
        for match in self.faults.finditer(self.data):
            line_end = self.data.find(b'\n', match.start())
            line = bytes(self.data[match.start():line_end if line_end >= 0 else len(self.data)])
            if self.expected_assert and (self.expected_assert.search(line) or line.rstrip() == b'Assertion failed'):
                # ARMv7-M prints one generic preamble before the file/line.
                # The expire verdict still requires the exact detail below.
                continue
            # Wait for a complete assertion line before deciding its origin.
            if self.expected_assert and line_end < 0 and line.startswith(b'Assertion failed'):
                continue
            raise RuntimeError('Unexpected target fault: ' + line.decode('utf-8', 'replace'))

    def pump(self, interval=0.2):
        for key, _ in self.selector.select(interval):
            chunk = os.read(key.fileobj.fileno(), 65536)
            if not chunk:
                raise RuntimeError('QEMU output closed before completion')
            self.data.extend(chunk)
            self.log.write(chunk)
            self.log.flush()
        self.check_faults()
        if self.process.poll() is not None:
            raise RuntimeError('QEMU exited before completion')

    def wait(self, pattern, timeout=None, allow_peer_failure=False):
        pattern = re.compile(pattern)
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            self.check_faults()
            match = pattern.search(self.data, self.cursor)
            if match:
                segment = bytes(self.data[self.cursor:match.end()]).decode('utf-8', 'replace')
                self.cursor = match.end()
                return segment
            if time.monotonic() >= deadline:
                raise TimeoutError('Missing serial marker: ' + repr(pattern.pattern))
            self.pump()

    def observe(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.pump(min(0.2, deadline - time.monotonic()))


def snapshot(qemu, expected_ids=None):
    qemu.wait_for_exit('health_monitor')
    ps = qemu.shell('ps', ['PID | PRIO'])
    ids = [int(line.split('|', 1)[0]) for line in ps.splitlines() if re.match(r'^\s*\d+\s*\|', line)]
    if not ids or (expected_ids is not None and sorted(ids) != sorted(expected_ids)):
        raise RuntimeError('Task inventory changed: ' + repr(ids))
    free = qemu.shell('free')
    match = re.search(r'Mem:\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', free)
    if not match:
        raise RuntimeError('Missing heap counters')
    total, used, available, largest = map(int, match.groups())
    if total != used + available or largest > available:
        raise RuntimeError('Invalid heap counters')
    return {'task_ids': ids, 'task_count': len(ids), 'used': used, 'free': available,
            'largest': largest, 'ps_output': ps, 'free_output': free}


def command(qemu, text, marker, timeout=30):
    start = time.monotonic()
    qemu.send(text)
    output = qemu.wait(re.escape(text.encode()) + rb'\r?\n')
    output += qemu.wait(re.escape(marker.encode()), timeout)
    output += qemu.wait_for_exit('health_monitor')
    if re.search(r'health_monitor: .*FAILED|result=-|pthread setup failed|join failed|open failed', output):
        raise RuntimeError('Health Monitor command failed: ' + text)
    return {'command': text, 'status': 'pass', 'elapsed_seconds': round(time.monotonic() - start, 3), 'output': output}


def normal(qemu, result):
    baseline = snapshot(qemu)
    result['baseline'] = baseline
    qemu.observe(3)
    result['idle_without_registration'] = snapshot(qemu, baseline['task_ids'])
    result['invalid_inputs'] = []
    for cmd in ('health_monitor run 0 1 1', 'health_monitor run 2000 0 1',
                'health_monitor run 2000 2000 1', 'health_monitor run 2000 250 0',
                'health_monitor stop 60001', 'health_monitor exit 2000 0'):
        result['invalid_inputs'].append(command(qemu, cmd, 'rounds 1..1000'))
    result['smp_rejected_on_up'] = command(qemu, 'health_monitor smp 5000 1',
                                            'health_monitor: smp requires at least two CPUs')
    result['rounds'] = []
    all_pids = []
    for number in range(1, 4):
        step = {'number': number, 'checks': []}
        result['rounds'].append(step)
        step['checks'].append(command(qemu, 'health_monitor run 2000 250 20', 'health_monitor: run completed'))
        qemu.observe(2.2)
        snapshot(qemu, baseline['task_ids'])
        step['checks'].append(command(qemu, 'health_monitor stop 2000', 'health_monitor: stop completed'))
        record = command(qemu, 'health_monitor exit 5000 100', 'health_monitor: exit completed')
        workers = re.findall(r'health_monitor: round=(\d+) worker=0 pid=(\d+) result=0', record['output'])
        if [int(n) for n, _ in workers] != list(range(1, 101)):
            raise RuntimeError('Missing successful worker-exit rounds')
        pids = [int(pid) for _, pid in workers]
        record['worker_pids'] = pids
        all_pids.extend(pids)
        step['checks'].append(record)
        step['snapshot'] = snapshot(qemu, baseline['task_ids'])
    warm = result['rounds'][0]['snapshot']
    final = result['rounds'][-1]['snapshot']
    result['heap_growth_after_warmup'] = final['used'] - warm['used']
    if result['heap_growth_after_warmup'] > 0:
        raise RuntimeError('Heap use grew after warmup')
    result['worker_exits'] = len(all_pids)
    result['unique_worker_pids'] = len(set(all_pids))
    result['pid_reuse_observed'] = len(set(all_pids)) < len(all_pids)
    result['tc_regression'] = {'status': 'running'}
    qemu.send('kernel_tc')
    output = qemu.wait(rb'Kernel TC Start')
    output += qemu.wait(rb'Kernel TC End \[PASS\s*:\s*\d+, FAIL\s*:\s*\d+\]', 900)
    totals = re.search(r'Kernel TC End \[PASS\s*:\s*(\d+), FAIL\s*:\s*(\d+)\]', output)
    passed, failed = map(int, totals.groups())
    result['tc_regression'] = {'pass': passed, 'fail': failed, 'output': output}
    if (passed, failed) != (433, 0) or re.search(r'\] FAIL\b|TC Assertion FAIL', output):
        raise RuntimeError('Kernel testcase regression')
    qemu.wait_for_exit('kernel_tc')
    result['tc_regression']['status'] = 'pass'
    result['post_regression_health'] = command(qemu, 'health_monitor run 2000 250 20', 'health_monitor: run completed')
    result['final_snapshot'] = snapshot(qemu, baseline['task_ids'])


def expire(qemu, result, milliseconds, source_line):
    result['baseline'] = snapshot(qemu)
    text = 'health_monitor expire ' + str(milliseconds)
    qemu.send(text)
    qemu.wait(re.escape(text.encode()) + rb'\r?\n')
    notice = qemu.wait(rb'health_monitor: deliberate timeout; expect PANIC/reboot reason 62\r?\n')
    pattern = rb'Assertion failed at file:[^\r\n]*health_monitor/health_monitor\.c line:\s*' + str(source_line).encode() + rb'\b'
    qemu.expected_assert = re.compile(pattern)
    start = time.monotonic()
    result['notice'] = notice
    result['assertion_output'] = qemu.wait(pattern, 20)
    result['observed_seconds_after_notice'] = round(time.monotonic() - start, 3)
    expected_seconds = milliseconds / 1000.0
    tolerance = max(0.15, expected_seconds * 0.05)
    result['wall_time_tolerance_seconds'] = tolerance
    if abs(result['observed_seconds_after_notice'] - expected_seconds) > tolerance:
        raise RuntimeError('Expiration wall time does not match the configured timeout')
    qemu.wait(rb'No heap corruption detected', 10)
    qemu.observe(1)
    assertions = re.findall(rb'Assertion failed at file:[^\r\n]*', qemu.data)
    preambles = re.findall(rb'up_assert: Assertion failed\r?\n', qemu.data)
    if len(assertions) != 1 or len(preambles) != 1 or not qemu.expected_assert.search(assertions[0]):
        raise RuntimeError('Expected one assertion preamble and one exact Health Monitor detail')
    if not re.search(rb'up_dumpstate: IRQ num:\s*15\b', qemu.data):
        raise RuntimeError('Expected Health Monitor assertion from SysTick IRQ 15')
    if b'health_monitor: expire FAILED' in qemu.data or b'health_monitor: expire completed' in qemu.data:
        raise RuntimeError('Expiry command returned after PANIC')
    result.update(timeout_ms=milliseconds, panic_line=source_line, expected_panic_observed=True,
                  assertion=assertions[0].decode('utf-8', 'replace'),
                  reset_verified=False, reboot_reason_verified=False)


def inside(args):
    result = {'status': 'fail', 'mode': args.mode, 'scope': 'LM3S6965 single-core, 10ms tick, PM and IRQ watchdog disabled'}
    qemu = HealthSession(args.output)
    try:
        boot = qemu.start(timeout=60)
        if 'QEMU health monitor: /dev/health_monitor ready' not in boot:
            raise RuntimeError('Health Monitor device registration did not complete')
        result['help'] = qemu.shell('help', ['health_monitor'])
        result['devices'] = qemu.shell('ls /dev', ['health_monitor'])
        if args.mode == 'normal':
            normal(qemu, result)
        else:
            expire(qemu, result, args.expire_ms, args.panic_line)
        result['status'] = 'pass'
    except Exception as exc:
        result['error'] = str(exc)
    finally:
        qemu.finish(result)
        try:
            qemu.check_faults()
        except Exception as exc:
            result.update(status='fail', error=str(exc))
        save_result(args.output, result)
    return 0 if result['status'] == 'pass' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('normal', 'expire'), required=True)
    parser.add_argument('--expire-ms', type=int, default=2000)
    parser.add_argument('--panic-line', type=int)
    args = parse_smoke_args(parser, 1200)
    if args.panic_line is None:
        source_root = Path('/work') if args.inside else args.root
        source = (source_root / 'os/kernel/health_monitor/health_monitor.c').read_text().splitlines()
        lines = [i + 1 for i, line in enumerate(source) if line.strip() == 'PANIC();']
        if len(lines) != 1:
            parser.error('Cannot uniquely identify the Health Monitor PANIC line')
        args.panic_line = lines[0]
    if args.inside:
        return inside(args)
    extra = ['--mode', args.mode, '--expire-ms', str(args.expire_ms), '--panic-line', str(args.panic_line)]
    return run_container(args.root, args.output, args.image, 'health-monitor-smoke.py', extra, args.timeout,
                         {'temporary_health_integration': True, 'health_feature_commit': '8d10c93bd79c69fb2b64ab420111a415e3d17eeb'})


if __name__ == '__main__':
    sys.exit(main())
