#!/usr/bin/env python3
"""Validate XIP dependencies using MPU permission faults, NOT flash bus stalls.

0: flash-accessible control; 1: blocked flash entry; 2: RAM entry/flash data;
3: complete RAM recovery. Negative cases require QEMU's explicit lockup and
the matching instruction/data access violation, never just silence/timeout.
Positive cases require a guest reset, retained record and fresh TASH command.
No debugger writes, host reset or guest relaunch are used within a case.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import resource
import select
import shutil
import socket
import subprocess
import tempfile
import time

from run import Elf


def no_core_dump():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run_case(args, case, out):
    out.mkdir(parents=True)
    result = dict(case=case, passed=False, model='mpu-permission-fault', events=[],
                  started_at=datetime.now(timezone.utc).isoformat())
    serial, pending = bytearray(), bytearray()
    process = None
    with tempfile.TemporaryDirectory(prefix='xiplab-') as tmp, \
            (out / 'stderr.log').open('wb') as err:
        qmp_path = str(Path(tmp) / 'qmp')
        command = [args.qemu, '-M', 'mps2-an505', '-kernel', str(args.elf),
                   '-display', 'none', '-monitor', 'none', '-serial', 'stdio',
                   '-nic', 'none', '-qmp', f'unix:{qmp_path},server=on,wait=off',
                   '-d', 'int,guest_errors', '-D', str(out / 'exceptions.log')]
        result['command'] = command
        try:
            process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=err,
                                       preexec_fn=no_core_dump)
            with socket.socket(socket.AF_UNIX) as qmp:
                deadline = time.monotonic() + args.timeout
                while not Path(qmp_path).exists():
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('QEMU did not start')
                    time.sleep(.01)
                qmp.connect(qmp_path)
                qmp.sendall(b'{"execute":"qmp_capabilities"}\r\n')
                sent = checked = False
                start = 0
                while time.monotonic() < deadline:
                    ready, _, _ = select.select([process.stdout, qmp], [], [], .05)
                    if process.stdout in ready:
                        serial.extend(process.stdout.read1(65536))
                    if qmp in ready:
                        pending.extend(qmp.recv(65536))
                        while b'\n' in pending:
                            line, _, rest = pending.partition(b'\n')
                            pending[:] = rest
                            event = json.loads(line)
                            if 'event' in event and sent:
                                result['events'].append(event)
                    if not sent and b'TASH>>' in serial:
                        start = len(serial)
                        process.stdin.write(f'xiplab {case}\n'.encode())
                        process.stdin.flush()
                        sent = True
                        deadline = time.monotonic() + args.timeout
                    body = bytes(serial[start:]) if sent else b''
                    reset = any(e['event'] == 'RESET' and
                                e.get('data', {}).get('guest') is True
                                for e in result['events'])
                    if process.poll() is not None:
                        if case not in (1, 2):
                            raise RuntimeError('QEMU exited instead of guest recovery')
                        error = (out / 'stderr.log').read_text()
                        trace = (out / 'exceptions.log').read_text()
                        expected = 'IACCVIOL' if case == 1 else 'DACCVIOL'
                        expected_pc = args.flash_entry if case == 1 else args.dependency_load
                        expected_address = args.flash_entry if case == 1 else args.flash_word
                        pc = re.search(r'R15=([0-9a-fA-F]{8})', error)
                        result['violation'] = expected
                        result['fault_pc'] = pc[1] if pc else None
                        result['fault_address'] = f'{expected_address:08x}'
                        result['passed'] = (sent and not reset
                            and f'XIPLAB arm case={case} model=mpu'.encode() in body
                            and "Lockup: can't escalate" in error
                            and 'current priority -1' in error
                            and expected in trace and pc is not None
                            and int(pc[1], 16) == expected_pc
                            and f'at fault address 0x{expected_address:x}' in trace)
                        result['outcome'] = 'architectural-lockup' if result['passed'] else 'unexpected-exit'
                        break
                    if case in (0, 3) and sent and not checked and b'TASH>>' in body:
                        match = re.search(rb'XIPLAB retained valid=1 ([^\r\n]+)', body)
                        fields = dict(re.findall(rb'(\w+)=([0-9a-f]+)', match[1])) if match else {}
                        result['record'] = {k.decode(): v.decode() for k, v in fields.items()}
                        if not (reset and fields.get(b'case') == str(case).encode()
                                and fields.get(b'stage') == b'30'
                                and fields.get(b'exception') == b'3'
                                and int(fields.get(b'pc', b'0'), 16) == args.trigger_pc
                                and int(fields.get(b'cfsr', b'0'), 16) == 0x10000
                                and int(fields.get(b'hfsr', b'0'), 16) & 0x40000000
                                and fields.get(b'mpu') == b'00000003'):
                            raise RuntimeError('prompt without required reset/record/MPU evidence')
                        process.stdin.write(b'xiplab\n')
                        process.stdin.flush()
                        checked = True
                        start = len(serial)
                    elif checked and b'usage: xiplab <0..3>' in body and b'TASH>>' in body:
                        result['passed'] = True
                        result['outcome'] = 'retained-record-guest-reset-tash'
                        break
                if not result['passed']:
                    raise RuntimeError('required observation missing (timeout is not a pass)')
        except (OSError, RuntimeError, ValueError) as exc:
            result['error'] = str(exc)
        finally:
            if process:
                result['qemu_returncode_before_cleanup'] = process.poll()
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                serial.extend(process.stdout.read())
            (out / 'serial.log').write_bytes(serial)
            (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=Path('build/output/bin/tinyara'))
    parser.add_argument('--config', type=Path, default=Path('os/.config'))
    parser.add_argument('--qemu', default='qemu-system-arm')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', default='0,1,2,3')
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=8)
    args = parser.parse_args()
    cases = [int(v) for v in args.cases.split(',')]
    if args.repeat < 1 or args.timeout <= 0 or not cases or any(c not in range(4) for c in cases):
        parser.error('cases must be 0..3; repeat and timeout must be positive')
    args.elf = args.elf.resolve()
    elf = Elf(args.elf)
    args.trigger_pc = elf.address('xiplab_trigger')
    args.flash_entry = elf.address('xiplab_flash_entry')
    args.dependency_load = elf.address('xiplab_dependency_load')
    args.flash_word = elf.address('g_xiplab_flash_word')
    config = args.config.read_text()
    if 'CONFIG_ARCH_CHIP_QEMU_ARMV8M=y' not in config or 'CONFIG_ARMV8M_XIP_FAULT_LAB=y' not in config:
        parser.error('requires qemu-armv8m/xip_fault_lab')
    ram_symbols = ['xiplab_ram_entry', 'xiplab_capture', 'xiplab_checksum',
                   'xiplab_arm', 'xiplab_region', 'xiplab_trigger',
                   'xiplab_read_dependency', 'g_xiplab_record',
                   'g_xiplab_vectors', 'g_xiplab_stack']
    flash_symbols = ['xiplab_flash_entry', 'g_xiplab_flash_word']
    layout = {s: elf.address(s) for s in ram_symbols + flash_symbols}
    if not (all(0x80000000 <= layout[s] < 0x81000000 for s in ram_symbols)
            and all(0x10000000 <= layout[s] < 0x10380000 for s in flash_symbols)):
        parser.error('ELF does not have the required RAM/flash placement')
    args.out.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(args.config, args.out / 'effective.config')
    sources = ['os/arch/arm/src/armv8-m/up_xip_lab.c',
               'os/arch/arm/src/armv8-m/up_xip_lab.h',
               'os/arch/arm/src/armv8-m/up_xip_lab_entry.S',
               'build/configs/qemu-armv8m/scripts/mps2-an505.ld',
               'tools/qemu-fault-lab/xip.py']
    metadata = dict(elf_sha256=hashlib.sha256(args.elf.read_bytes()).hexdigest(),
                    config_sha256=hashlib.sha256(args.config.read_bytes()).hexdigest(),
                    qemu=subprocess.check_output([args.qemu, '--version'], text=True).splitlines()[0],
                    head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                    layout={k: f'{v:08x}' for k, v in layout.items()},
                    working_source_sha256={p: hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in sources},
                    limitation='MPU permission faults; not SPIC, bus wait-state or T32 emulation')
    (args.out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    results = []
    for repeat in range(args.repeat):
        for case in cases:
            result = run_case(args, case, args.out / f'{case}-{repeat + 1}')
            results.append(result)
            print(f"case={case} repeat={repeat + 1} passed={result['passed']} "
                  f"{result.get('error', result.get('outcome', ''))}", flush=True)
    (args.out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
