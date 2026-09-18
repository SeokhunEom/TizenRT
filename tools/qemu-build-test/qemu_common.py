"""Shared QEMU process, evidence handling and repeated smoke checks (Python 3.5+)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import time


class QemuSession:
    def __init__(self, output, timeout=90):
        self.output = output
        self.timeout = timeout
        self.started = time.monotonic()
        self.process = None
        self.selector = selectors.DefaultSelector()
        self.log = (output / 'serial.log').open('wb')
        self.data = bytearray()
        self.cursor = 0

    def start(self, network=False, timeout=None):
        self.command = ['qemu-system-arm', '-M', 'lm3s6965evb', '-kernel',
                        '/work/build/output/bin/tinyara', '-nographic', '-monitor', 'none']
        if network:
            self.command.extend(['-net', 'nic,macaddr=52:54:00:12:34:56', '-net',
                                 'user,hostfwd=tcp:127.0.0.1:40001-10.0.2.15:15001,'
                                 'hostfwd=udp:127.0.0.1:40002-10.0.2.15:15002'])
        else:
            self.command.extend(['-net', 'none'])
        self.process = subprocess.Popen(self.command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, bufsize=0)
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        return self.wait(rb'TASH>>', timeout) + self.wait(rb'execute them in TASH', timeout)

    def wait(self, pattern, timeout=None, allow_peer_failure=False):
        pattern = re.compile(pattern)
        faults = rb'Assertion failed|PANIC|up_assert:|HardFault|BusFault|UsageFault|qemu: fatal|CONSTRUCTION FAILED'
        if not allow_peer_failure:
            faults += rb'|NETPEER FAIL'
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            if re.search(faults, self.data, re.I):
                raise RuntimeError('Target fault or peer failure in serial output')
            match = pattern.search(self.data, self.cursor)
            if match:
                segment = bytes(self.data[self.cursor:match.end()]).decode('utf-8', 'replace')
                self.cursor = match.end()
                return segment
            if time.monotonic() >= deadline:
                raise TimeoutError('Missing serial marker: ' + repr(pattern.pattern))
            for key, _ in self.selector.select(0.2):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    raise RuntimeError('QEMU output closed before completion')
                self.data.extend(chunk)
                self.log.write(chunk)
                self.log.flush()
            if self.process.poll() is not None:
                raise RuntimeError('QEMU exited before completion')

    def send(self, command):
        self.process.stdin.write((command + '\n').encode())
        self.process.stdin.flush()

    def shell(self, command, required=(), timeout=None):
        self.send(command)
        if command:
            self.wait(re.escape(command.encode()) + rb'\r?\n')
        response = self.wait(rb'TASH>>', timeout=timeout)
        for marker in required:
            if marker not in response:
                raise RuntimeError(command + ': missing ' + marker)
        return response

    def wait_for_exit(self, name, timeout=10):
        deadline = time.monotonic() + timeout
        while True:
            output = self.shell('ps', ['PID | PRIO'])
            if not re.search(r'(?m)\|\s*' + re.escape(name) + r'\s*$', output):
                return output
            if time.monotonic() >= deadline:
                raise RuntimeError(name + ' printed its final marker but did not exit')
            time.sleep(0.05)

    def close(self):
        try:
            if self.process is not None:
                if self.process.poll() is None:
                    self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
                remainder = self.process.stdout.read()
                self.log.write(remainder)
                self.data.extend(remainder)
        finally:
            self.log.close()
            self.selector.close()

    def finish(self, result):
        try:
            self.close()
        except Exception as exc:
            result['status'] = 'fail'
            result['cleanup_error'] = str(exc)
        result['elapsed_seconds'] = round(time.monotonic() - self.started, 3)
        result['qemu_command'] = getattr(self, 'command', None)
        result['qemu_exit_after_cleanup'] = self.process.returncode if self.process is not None else None
        save_result(self.output, result)
        print(json.dumps({key: result.get(key) for key in
                          ('status', 'active_step', 'error', 'cleanup_error', 'elapsed_seconds')}, indent=2))


def save_result(output, result):
    temporary = output / 'result.tmp'
    temporary.write_text(json.dumps(result, indent=2) + '\n')
    temporary.replace(output / 'result.json')


def parse_smoke_args(parser, timeout):
    parser.add_argument('--root', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--image', default='tizenrt/qemu-build-test:2.12.0-16m')
    parser.add_argument('--timeout', type=float, default=timeout)
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    if not args.inside and args.root is None:
        parser.error('--root is required')
    return args


def check_helloxx(qemu, version=201103):
    qemu.send('helloxx')
    output = ''.join(qemu.wait(rb'CHelloWorld::HelloWorld: Hello, World!!') for _ in range(3))
    markers = ('dynamically constructed instance', 'constructed on the stack',
               'statically constructed instance', 'c++ version used : ' + str(version))
    if any(marker not in output for marker in markers):
        raise RuntimeError('Incomplete C++ constructor coverage or wrong language version')
    return {'name': 'helloxx', 'status': 'pass', 'output': output, 'post_ps': qemu.wait_for_exit('helloxx')}


def check_cxxtest(qemu):
    qemu.send('cxxtest')
    output = qemu.wait(rb'thread::join test') + qemu.wait(rb'All threads joined!')
    markers = ('Successfully opened /dev/console', 'Writing this to /dev/console', 'Test iostream',
               'Test STL(Array)', 'Test STL(Vectors)', 'v1=1 2 3', 'Test STL(Map)', 'Test STL(List)',
               'Test STL(Tuple)', 'Test STL(Function Templates)', 'Test RTTI', 'extend',
               'Catch exception: runtime error', 'thread::operator= test', 'thread::joinable test',
               'bar: true', 'bar: false', 'pause of 5 seconds ended')
    if any(marker not in output for marker in markers) or output.count('All threads joined!') != 2:
        raise RuntimeError('Incomplete C++ runtime coverage')
    return {'name': 'cxxtest', 'status': 'pass', 'output': output, 'post_ps': qemu.wait_for_exit('cxxtest')}


def storage_command(qemu, command, required=(), timeout=None):
    output = qemu.shell(command, required, timeout=timeout)
    if re.search(r'ERROR:|Unable to|Failed|failure|Seek error|^Fail\r?$|is failed', output, re.I | re.M):
        raise RuntimeError('Storage command failed: ' + command)
    return output


def check_smartfs(command, loops, filename):
    output = command('smart_test -s 50 -w 50 -l 100 -c 32 -t 16 -e 4 -r 64 ' + filename,
                     ('Performing 50 random seek tests', 'Performing 50 random seek with write tests',
                      'Performing 32 circular log record update tests'))
    if len(re.findall(r'(?m)^Pass\r?$', output)) != 2:
        raise RuntimeError('Missing SmartFS write/circular-log pass markers')
    command('rm ' + filename)
    output = command('smart', ('Final memory usage:',))
    fills = list(map(int, re.findall(r'=== FILLING (\d+) ', output)))
    deletes = list(map(int, re.findall(r'=== DELETING (\d+) ', output)))
    counts = list(map(int, re.findall(r'Number of files:\s*(\d+)', output)))
    if loops <= 0 or fills != list(range(1, loops + 1)) or fills != deletes or len(counts) != loops * 2 or min(counts) <= 0:
        raise RuntimeError('Incomplete SmartFS fill/delete workload')
    return {'status': 'pass', 'loops': loops, 'minimum_file_count': min(counts)}


def run_container(root, output, image, script, extra, timeout, extra_metadata=None):
    """Run one checker in an isolated container and attach firmware provenance."""
    root = Path(root).resolve()
    out = Path(os.path.realpath(str(output)))
    if out.exists() and any(out.iterdir()):
        raise ValueError('--output must be new or empty')
    out.mkdir(parents=True, exist_ok=True)
    name = 'codex-qemu-test-' + str(os.getpid())
    # Keep evidence writable by the caller on native Linux bind mounts.
    command = ['docker', 'run', '--rm', '--pull=never', '--platform', 'linux/arm64', '--name', name,
               '--user', '{0}:{1}'.format(os.getuid(), os.getgid()),
               '--network', 'none', '-v', str(root) + ':/work:ro', '-v', str(out) + ':/evidence',
               image, 'python3', '/work/tools/qemu-build-test/' + script, '--inside', '--output', '/evidence']
    command.extend(extra)
    metadata = {'docker_command': command,
                'firmware_sha256': hashlib.sha256((root / 'build/output/bin/tinyara').read_bytes()).hexdigest(),
                'effective_config_sha256': hashlib.sha256((root / 'os/.config').read_bytes()).hexdigest(),
                'defconfig_sha256': hashlib.sha256((root / 'build/configs/qemu/build_test/defconfig').read_bytes()).hexdigest(),
                'source_commit': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD']).decode().strip(),
                'script': script}
    metadata.update(extra_metadata or {})
    error = None
    code = None
    started = time.monotonic()
    try:
        with (out / 'runner.log').open('wb') as log:
            code = subprocess.call(command, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
    except Exception as exc:
        error = str(exc)
    finally:
        try:
            subprocess.call(['docker', 'stop', '--time', '1', name], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, timeout=15)
        except Exception as exc:
            error = error or str(exc)
    path = out / 'result.json'
    result = json.loads(path.read_text()) if path.exists() else {'status': 'fail', 'error': 'No inner result'}
    result.update(metadata)
    result['container_exit'] = code
    result['elapsed_wall_seconds'] = round(time.monotonic() - started, 3)
    accepted = ('pass', 'pass_with_known_driver_failures', 'diagnostic_pass')
    if result['status'] not in accepted and code == 0:
        error = error or 'Invalid or incomplete inner verdict: ' + result['status']
    if error is not None or code != 0:
        result['status'] = 'fail'
        if error:
            result['runner_error'] = error
    save_result(out, result)
    print(json.dumps({key: result.get(key) for key in ('status', 'error', 'runner_error', 'elapsed_wall_seconds')}, indent=2))
    print('Evidence: ' + str(out))
    return 0 if result['status'] in accepted else 1
