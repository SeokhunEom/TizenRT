#!/usr/bin/env python3
"""Check a fresh LM3S6965 QEMU boot and three TASH commands."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--image', default='tizenrt/qemu-build-test:2.12.0-16m')
parser.add_argument('--timeout', type=float, default=60)
args = parser.parse_args()
root = args.root.resolve()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
firmware = root / 'build/output/bin/tinyara'
name = 'codex-qemu-build-test-smoke-' + str(os.getpid())
command = ['docker', 'run', '--rm', '--pull=never', '--platform', 'linux/arm64',
           '--name', name, '--network', 'none', '-i', '-v', str(root) + ':/work:ro',
           args.image, 'qemu-system-arm', '-M', 'lm3s6965evb', '-kernel',
           '/work/build/output/bin/tinyara', '-nographic', '-monitor', 'none', '-net', 'none']
result = {'status': 'fail', 'command': command, 'firmware_sha256': hashlib.sha256(firmware.read_bytes()).hexdigest(),
          'checks': [], 'timeout_seconds': args.timeout}
start = time.monotonic()
process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
selector = selectors.DefaultSelector()
selector.register(process.stdout, selectors.EVENT_READ)
transcript = bytearray()
cursor = 0
fault = re.compile(rb'Assertion failed|PANIC|HardFault|BusFault|UsageFault|qemu: fatal', re.I)
log = (out / 'serial.log').open('wb')

def prompt(marker=b'TASH>>'):
    global cursor
    deadline = time.monotonic() + args.timeout
    while True:
        if fault.search(transcript):
            raise RuntimeError('Fault marker in serial output')
        match = transcript.find(marker, cursor)
        if match >= 0:
            end = match + len(marker)
            segment = bytes(transcript[cursor:end])
            cursor = end
            return segment.decode('utf-8', 'replace')
        if time.monotonic() >= deadline:
            raise TimeoutError('Timed out waiting for serial marker ' + repr(marker))
        for key, _ in selector.select(timeout=0.2):
            data = os.read(key.fileobj.fileno(), 65536)
            if not data:
                raise RuntimeError('QEMU output closed before expected prompt')
            log.write(data)
            log.flush()
            transcript.extend(data)
        if process.poll() is not None:
            raise RuntimeError('QEMU exited before expected prompt')

try:
    # The shell starts before preapp finishes registering built-in commands.
    boot = prompt() + prompt(b'execute them in TASH')
    result['checks'].append({'name': 'boot', 'status': 'pass', 'output': boot})
    for cmd, required in [('help', ['kernel_tc', 'ps', 'free']), ('ps', ['PID', 'PRIO', 'tash']), ('free', ['total', 'Mem:'])]:
        process.stdin.write((cmd + '\n').encode())
        process.stdin.flush()
        response = prompt()
        missing = [word for word in required if word.lower() not in response.lower()]
        if missing:
            raise RuntimeError(cmd + ': missing output markers ' + repr(missing))
        result['checks'].append({'name': cmd, 'status': 'pass', 'output': response})
    result['status'] = 'pass'
except Exception as exc:
    result['error'] = str(exc)
finally:
    subprocess.run(['docker', 'stop', '--time', '1', name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    remainder = process.stdout.read()
    if remainder:
        log.write(remainder)
        transcript.extend(remainder)
    log.close()
    selector.close()
    result['elapsed_seconds'] = round(time.monotonic() - start, 3)
    result['container_exit_after_cleanup'] = process.returncode
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': result['status'], 'checks': [check['name'] for check in result['checks']], 'elapsed_seconds': result['elapsed_seconds'], 'result': str(out / 'result.json'), 'error': result.get('error')}, indent=2))
raise SystemExit(0 if result['status'] == 'pass' else 1)
