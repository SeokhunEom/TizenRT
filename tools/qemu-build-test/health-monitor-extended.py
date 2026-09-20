#!/usr/bin/env python3
"""Real UP QEMU Health Monitor contract/lifecycle and controlled-time tests."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys
import time
from qemu_common import parse_smoke_args, run_container, save_result

spec = importlib.util.spec_from_file_location('hm_smoke', str(Path(__file__).with_name('health-monitor-smoke.py')))
hm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hm)
NORMAL = ('api', 'shared', 'lifecycle', 'reuse', 'boundary', 'active', 'stress')
FATAL = ('close', 'equality', 'overdue', 'late', 'wrap', 'stale', 'multi', 'equal', 'far', 'unstable')

class Session(hm.HealthSession):
    def check_faults(self):
        super().check_faults()
        if b'HM_QEMU FAIL' in self.data:
            raise RuntimeError('QEMU fixture reported a failed check')


def snapshot(q):
    q.wait_for_exit('hm_qemu')
    return hm.snapshot(q)


def run_case(q, case):
    started = time.monotonic()
    q.send('hm_qemu ' + case)
    output = q.wait(('HM_QEMU BEGIN case=' + case + r' capacity=\d+').encode())
    output += q.wait(('HM_QEMU PASS case=' + case + r' checks=\d+ count=0').encode(), 90)
    q.wait_for_exit('hm_qemu')
    count = int(re.search(r'checks=(\d+) count=0', output).group(1))
    if count < 10:
        raise RuntimeError('Incomplete fixture checks: ' + case)
    return {'case': case, 'status': 'pass', 'checks': count,
            'elapsed_seconds': round(time.monotonic() - started, 3), 'output': output}


def fatal(q, case, result):
    source = (getattr(q, 'root', Path('/work')) / 'os/kernel/health_monitor/health_monitor.c').read_text().splitlines()
    lines = [i + 1 for i, line in enumerate(source) if line.strip() == 'PANIC();']
    if len(lines) != 1:
        raise RuntimeError('Cannot uniquely identify Health Monitor PANIC line')
    pattern = rb'Assertion failed at file:[^\r\n]*health_monitor/health_monitor\.c line:\s*' + str(lines[0]).encode() + rb'\b'
    q.expected_assert = re.compile(pattern)
    started = time.monotonic()
    q.send('hm_qemu fatal ' + case)
    output = q.wait(rb'HM_QEMU EXPECT case=[^\r\n]+\r?\n', 20)
    expected = re.search(r'HM_QEMU EXPECT case=(\w+) pid=(\d+) (?:now=(\d+) )?deadline=(\d+) (?:natural|controlled)=1', output)
    if not expected or expected.group(1) != case:
        raise RuntimeError('Missing expected target/deadline')
    output += q.wait(pattern, 20)
    output += q.wait(rb'No heap corruption detected', 10)
    q.observe(0.2)
    text = q.data.decode('utf-8', 'replace')
    verdicts = re.findall(r'HM_QEMU verdict pid=(\d+) now=(\d+) deadline=(\d+)', text)
    if len(verdicts) != 1:
        raise RuntimeError('Expected one measured expiration verdict')
    pid, now, deadline = map(int, verdicts[0])
    if pid != int(expected.group(2)) or deadline != int(expected.group(4)):
        raise RuntimeError('Wrong task or deadline expired')
    if expected.group(3) is not None and now != int(expected.group(3)):
        raise RuntimeError('Wrong controlled inspection time')
    if ((now - deadline) & 0xffffffff) >= 0x80000000:
        raise RuntimeError('PANIC occurred before the registered deadline')
    if len(re.findall(r'Assertion failed at file:', text)) != 1 or not re.search(r'IRQ num:\s*' + str(getattr(q, 'timer_irq', 15)) + r'\b', text):
        raise RuntimeError('Expected one system-timer assertion')
    result.update(expected_panic=True, expired_pid=pid, now=now, deadline=deadline,
                  panic_line=lines[0], elapsed_seconds_until_collection=round(time.monotonic() - started, 3),
                  output=output, physical_reset_verified=False)


def inside(args):
    q = Session(args.output)
    result = {'status': 'fail', 'case': args.case, 'rounds': [], 'test_only_controls': True}
    try:
        boot = q.start(timeout=60)
        if 'QEMU health monitor: /dev/health_monitor ready' not in boot:
            raise RuntimeError('Health Monitor registration absent')
        q.shell('help', ['hm_qemu'])
        result['baseline'] = snapshot(q)
        if args.case.startswith('fatal-'):
            fatal(q, args.case[6:], result)
        else:
            cases = NORMAL if args.case == 'all' else (args.case,)
            for number in range(1, args.rounds + 1):
                record = {'round': number, 'checks': []}
                result['rounds'].append(record)
                for case in cases:
                    record['checks'].append(run_case(q, case))
                record['snapshot'] = snapshot(q)
                if record['snapshot']['task_ids'] != result['baseline']['task_ids']:
                    raise RuntimeError('Tasks remain after fixture completion')
            result['heap_growth_after_warmup'] = result['rounds'][-1]['snapshot']['used'] - result['rounds'][0]['snapshot']['used']
            if result['heap_growth_after_warmup'] > 0:
                raise RuntimeError('Heap increased after the first completed round')
        result['status'] = 'pass'
    except Exception as exc:
        result.update(status='fail', error=str(exc))
    finally:
        q.finish(result)
        try:
            q.check_faults()
        except Exception as exc:
            result.update(status='fail', error=str(exc))
        save_result(args.output, result)
    return 0 if result['status'] == 'pass' else 1


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case', choices=('all', 'capacity') + NORMAL + tuple('fatal-' + x for x in FATAL), required=True)
    p.add_argument('--rounds', type=int, default=1)
    args = parse_smoke_args(p, 600)
    if args.inside:
        return inside(args)
    return run_container(args.root, args.output, args.image, 'health-monitor-extended.py',
                         ['--case', args.case, '--rounds', str(args.rounds)], args.timeout)

if __name__ == '__main__':
    sys.exit(main())
