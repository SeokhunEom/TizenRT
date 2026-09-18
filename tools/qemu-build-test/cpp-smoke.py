#!/usr/bin/env python3
"""Verify C++ constructors and the existing RTTI/exception/STL/thread example in QEMU."""
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
parser.add_argument('--basic-only', action='store_true', help='Run helloxx without cxxtest')
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--image', default='tizenrt/qemu-build-test:2.12.0-16m')
parser.add_argument('--timeout', type=float, default=60)
args = parser.parse_args()
if args.timeout <= 0:
    parser.error('--timeout must be positive')
root = args.root.resolve()
config = (root / 'os/.config').read_text()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
firmware = root / 'build/output/bin/tinyara'
name = 'codex-qemu-cpp-smoke-' + str(os.getpid())
command = ['docker', 'run', '--rm', '--pull=never', '--platform', 'linux/arm64',
           '--name', name, '--network', 'none', '-i', '-v', str(root) + ':/work:ro',
           args.image, 'qemu-system-arm', '-M', 'lm3s6965evb', '-kernel',
           '/work/build/output/bin/tinyara', '-nographic', '-monitor', 'none', '-net', 'none']
result = {'status': 'fail', 'command': command, 'firmware_sha256': hashlib.sha256(firmware.read_bytes()).hexdigest(),
          'checks': [], 'timeout_seconds': args.timeout,
          'effective_config_sha256': hashlib.sha256(config.encode()).hexdigest(),
          'source_commit': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()}
start = time.monotonic()
process = None
selector = selectors.DefaultSelector()
transcript = bytearray()
cursor = 0
fault = re.compile(rb'Assertion failed|PANIC|HardFault|BusFault|UsageFault|qemu: fatal|CONSTRUCTION FAILED', re.I)
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

def send(cmd):
    process.stdin.write((cmd + '\n').encode())
    process.stdin.flush()

def shell(cmd):
    send(cmd)
    return prompt()

def exited(name):
    deadline = time.monotonic() + 10
    while True:
        output = shell('ps')
        while 'PID | PRIO' not in output:
            output += prompt()
        if not re.search(r'(?m)\|\s*' + re.escape(name) + r'\s*$', output):
            return output
        if time.monotonic() >= deadline:
            raise RuntimeError(name + ' printed its final marker but did not exit')

try:
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    selector.register(process.stdout, selectors.EVENT_READ)
    boot = prompt() + prompt(b'execute them in TASH')
    result['checks'].append({'name': 'boot', 'status': 'pass', 'output': boot})
    commands = shell('help')
    result['inventory'] = {'help': commands, 'mount': shell('mount')}
    required_commands = ('helloxx',) if args.basic_only else ('helloxx', 'cxxtest')
    if any(name not in commands for name in required_commands):
        raise RuntimeError('Required C++ command is absent from TASH')
    send('helloxx')
    output = ''.join(prompt(b'CHelloWorld::HelloWorld: Hello, World!!') for _ in range(3))
    for marker in ('dynamically constructed instance', 'constructed on the stack', 'statically constructed instance'):
        if marker not in output:
            raise RuntimeError('Missing constructor coverage: ' + marker)
    version = 201703 if 'CONFIG_CXX_VERSION_17=y' in config else 201402 if 'CONFIG_CXX_VERSION_14=y' in config else 201103
    if 'c++ version used : ' + str(version) not in output:
        raise RuntimeError('Compiled C++ language version did not match the config')
    result['checks'].append({'name': 'helloxx', 'status': 'pass', 'output': output, 'post_ps': exited('helloxx')})
    if not args.basic_only:
        send('cxxtest')
        output = prompt(b'thread::join test') + prompt(b'All threads joined!')
        required = ('Successfully opened /dev/console', 'Writing this to /dev/console',
                    'Test iostream', 'Test STL(Array)', 'Test STL(Vectors)', 'v1=1 2 3',
                    'Test STL(Map)', 'Test STL(List)', 'Test STL(Tuple)', 'Test STL(Function Templates)',
                    'Test RTTI', 'extend', 'Catch exception: runtime error', 'thread::operator= test',
                    'thread::joinable test', 'bar: true', 'bar: false', 'pause of 5 seconds ended')
        if any(marker not in output for marker in required) or output.count('All threads joined!') != 2:
            raise RuntimeError('Incomplete cxxtest coverage')
        result['checks'].append({'name': 'cxxtest', 'status': 'pass', 'output': output, 'post_ps': exited('cxxtest')})
    result['post_free'] = shell('free')
    if 'Mem:' not in result['post_free']:
        raise RuntimeError('No memory report after C++ tests')
    result['status'] = 'pass'

except Exception as exc:
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
    log.close()
    selector.close()
    result['elapsed_seconds'] = round(time.monotonic() - start, 3)
    result['container_exit_after_cleanup'] = process.returncode if process is not None else None
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': result['status'], 'checks': [check['name'] for check in result['checks']], 'elapsed_seconds': result['elapsed_seconds'], 'result': str(out / 'result.json'), 'error': result.get('error')}, indent=2))
raise SystemExit(0 if result['status'] == 'pass' else 1)
