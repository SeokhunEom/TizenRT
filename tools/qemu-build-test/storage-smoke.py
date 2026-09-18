#!/usr/bin/env python3
"""Verify RAM-backed SmartFS commands, stress loops, and remount data."""
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
parser.add_argument('--timeout', type=float, default=300)
args = parser.parse_args()
if args.timeout <= 0:
    parser.error('--timeout must be positive')
root = args.root.resolve()
config = (root / 'os/.config').read_text()
loops = int(re.search(r'^CONFIG_EXAMPLES_SMART_NLOOPS=(\d+)$', config, re.M).group(1))
if loops <= 0:
    parser.error('storage-smoke requires a finite CONFIG_EXAMPLES_SMART_NLOOPS')
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
firmware = root / 'build/output/bin/tinyara'
name = 'codex-qemu-storage-smoke-' + str(os.getpid())
command = ['docker', 'run', '--rm', '--pull=never', '--platform', 'linux/arm64',
           '--name', name, '--network', 'none', '-i', '-v', str(root) + ':/work:ro',
           args.image, 'qemu-system-arm', '-M', 'lm3s6965evb', '-kernel',
           '/work/build/output/bin/tinyara', '-nographic', '-monitor', 'none', '-net', 'none']
result = {'status': 'fail', 'command': command, 'firmware_sha256': hashlib.sha256(firmware.read_bytes()).hexdigest(),
          'checks': [], 'timeout_seconds': args.timeout,
          'effective_config_sha256': hashlib.sha256(config.encode()).hexdigest(), 'expected_smart_loops': loops}
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

def shell(cmd, required=()):
    process.stdin.write((cmd + '\n').encode())
    process.stdin.flush()
    response = prompt()
    errors = [line for line in response.splitlines()
              if re.search(r'ERROR:|Unable to|Failed|failure|Seek error|^Fail$|is failed', line, re.I)]
    if errors or any(marker not in response for marker in required):
        raise RuntimeError(cmd + ': missing success marker or error: ' + repr(errors[:5]))
    result['checks'].append({'name': cmd, 'status': 'pass', 'output': response})
    return response

try:
    boot = prompt() + prompt(b'execute them in TASH')
    if 'QEMU storage: /mnt ready' not in boot:
        raise RuntimeError('Storage initialization did not complete')
    result['checks'].append({'name': 'boot', 'status': 'pass', 'output': boot})
    shell('ls /dev', ('smart0', 'smart1', 'mtdblock1'))
    shell('mount', ('/mnt type smartfs',))
    shell('echo QEMU-remount-data > /mnt/remount.txt')
    if 'QEMU-remount-data' not in shell('cat /mnt/remount.txt').splitlines():
        raise RuntimeError('File contents did not match after write')
    shell('umount /mnt')
    mounts = shell('mount')
    if '/mnt type smartfs' in mounts:
        raise RuntimeError('Unmount did not remove /mnt')
    shell('mount -t smartfs /dev/smart0 /mnt')
    if 'QEMU-remount-data' not in shell('cat /mnt/remount.txt').splitlines():
        raise RuntimeError('File contents did not survive remount')
    output = shell('smart_test -s 50 -w 50 -l 100 -c 32 -t 16 -e 4 -r 64 /mnt/stage03',
                   ('Performing 50 random seek tests', 'Performing 50 random seek with write tests',
                    'Performing 32 circular log record update tests'))
    if len(re.findall(r'(?m)^Pass\r?$', output)) != 2:
        raise RuntimeError('Missing smart_test write/circular-log success markers')
    shell('rm /mnt/stage03')
    output = shell('smart', ('Final memory usage:',))
    fills = re.findall(r'=== FILLING (\d+) ', output)
    deletes = re.findall(r'=== DELETING (\d+) ', output)
    if loops <= 0 or list(map(int, fills)) != list(range(1, loops + 1)) or fills != deletes:
        raise RuntimeError('Smart stress loops did not complete')
    counts = list(map(int, re.findall(r'Number of files:\s*(\d+)', output)))
    if len(counts) != loops * 2 or any(count <= 0 for count in counts):
        raise RuntimeError('Smart stress did not exercise populated files')
    result['smart_loops'] = loops
    result['minimum_file_count'] = min(counts)
    if 'QEMU-remount-data' not in shell('cat /mnt/remount.txt').splitlines():
        raise RuntimeError('Unrelated file was damaged by stress')
    shell('rm /mnt/remount.txt')
    shell('ls /mnt')
    shell('free', ('Mem:',))
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
