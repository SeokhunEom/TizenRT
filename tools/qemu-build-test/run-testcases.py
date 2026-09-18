#!/usr/bin/env python3
"""Run one existing build_test testcase in a fresh QEMU instance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import time

SUITES = {'kernel_tc': 'Kernel TC', 'drivers_tc': 'Drivers TC', 'filesystem_tc': 'FileSystem TC'}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--suite', choices=SUITES, required=True)
parser.add_argument('--timeout', type=float, default=600)
parser.add_argument('--image', default='tizenrt/qemu-build-test:2.12.0-16m')
args = parser.parse_args()
if args.timeout <= 0:
    parser.error('--timeout must be positive')
root = args.root.resolve()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
name = 'codex-qemu-stage02-' + str(os.getpid())
command = ['docker', 'run', '--rm', '--pull=never', '--platform', 'linux/arm64', '--name', name,
           '--network', 'none', '-i', '-v', str(root) + ':/work:ro', args.image,
           'qemu-system-arm', '-M', 'lm3s6965evb', '-kernel', '/work/build/output/bin/tinyara',
           '-nographic', '-monitor', 'none', '-net', 'none']
result = {'suite': args.suite, 'status': 'fail', 'completed': False, 'pass': None, 'fail': None,
          'command': command, 'timeout_seconds': args.timeout,
          'firmware_sha256': hashlib.sha256((root / 'build/output/bin/tinyara').read_bytes()).hexdigest(),
          'defconfig_sha256': hashlib.sha256((root / 'build/configs/qemu/build_test/defconfig').read_bytes()).hexdigest(),
          'source_commit': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()}
started = time.monotonic()
process = None
selector = selectors.DefaultSelector()
transcript = bytearray()
cursor = 0
log = (out / 'serial.log').open('wb')
fault = re.compile(rb'Assertion failed|PANIC|up_assert:|up_hardfault:|qemu: fatal', re.I)

def wait_for(pattern, timeout):
    global cursor
    deadline = time.monotonic() + timeout
    while True:
        problem = fault.search(transcript)
        if problem:
            raise RuntimeError('Target fault: ' + problem.group().decode('utf-8', 'replace'))
        match = pattern.search(transcript, cursor)
        if match:
            segment = bytes(transcript[cursor:match.end()])
            cursor = match.end()
            return match, segment.decode('utf-8', 'replace')
        if time.monotonic() >= deadline:
            raise TimeoutError('No completion marker within ' + str(timeout) + ' seconds')
        for key, _ in selector.select(timeout=0.2):
            data = os.read(key.fileobj.fileno(), 65536)
            if not data:
                raise RuntimeError('QEMU output closed before completion')
            transcript.extend(data)
            log.write(data)
            log.flush()
        if process.poll() is not None:
            raise RuntimeError('QEMU exited before completion')

def send(command):
    process.stdin.write((command + '\n').encode())
    process.stdin.flush()

def shell(command):
    send(command)
    _, output = wait_for(re.compile(rb'TASH>>'), 30)
    return output

try:
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    selector.register(process.stdout, selectors.EVENT_READ)
    wait_for(re.compile(rb'TASH>>'), 60)
    wait_for(re.compile(rb'execute them in TASH'), 60)
    result['inventory'] = {}
    for probe in ('help', 'ls /dev', 'mount', 'free'):
        result['inventory'][probe] = shell(probe)
    if args.suite not in result['inventory']['help']:
        raise RuntimeError('Requested suite is absent from the TASH command registry')
    suite_label = SUITES[args.suite].encode()
    send(args.suite)
    wait_for(re.compile(re.escape(suite_label) + rb' Start'), 30)
    # These commands are asynchronous. A shell prompt is not a completion signal.
    match, _ = wait_for(re.compile(re.escape(suite_label) + rb' End \[PASS\s*:\s*(\d+), FAIL\s*:\s*(\d+)\]'), args.timeout)
    result['completed'] = True
    result['pass'], result['fail'] = map(int, match.groups())
    result['status'] = 'pass' if result['pass'] > 0 and result['fail'] == 0 else 'fail'
    # Empty input obtains a new prompt after the asynchronous end marker.
    shell('')
    result['post_suite'] = {probe: shell(probe) for probe in ('ps', 'free')}
except Exception as exc:
    result['status'] = 'fail'
    result['error'] = str(exc)
finally:
    if process is not None:
        try:
            subprocess.run(['docker', 'stop', '--time', '1', name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
        except (subprocess.TimeoutExpired, OSError) as exc:
            result['cleanup_error'] = str(exc)
            result['status'] = 'fail'
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        remainder = process.stdout.read()
        if remainder:
            log.write(remainder)
            transcript.extend(remainder)
        result['container_exit_after_cleanup'] = process.returncode
    log.close()
    selector.close()
    result['elapsed_seconds'] = round(time.monotonic() - started, 3)
    result['observed_pass_lines'] = len(re.findall(rb'\] PASS\s*(?:\r?\n|$)', transcript))
    result['observed_fail_lines'] = len(re.findall(rb'\] FAIL\b', transcript))
    result['failure_lines'] = [line for line in transcript.decode('utf-8', 'replace').splitlines()
                               if '[FAIL]' in line or re.search(r'\]\s+FAIL\b', line)]
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('suite','status','completed','pass','fail','elapsed_seconds')}, indent=2))
    if 'error' in result:
        print(result['error'])
    print('Evidence: ' + str(out))
raise SystemExit(0 if result['status'] == 'pass' else 1)
