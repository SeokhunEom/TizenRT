#!/usr/bin/env python3
"""Strict guest recovery test. No debugger writes, host reset, or relaunch.

Each case starts a fresh QEMU. Cases 1-3 must return to TASH and run a second
command. Cases 4-6 must emit a QMP guest RESET, report a retained valid crash
record, and accept a command after reboot in the SAME QEMU process.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import select
import shutil
import socket
import subprocess
import tempfile
import time

from run import Elf


def run_case(args, case, out):
    out.mkdir(parents=True)
    result = dict(case=case, passed=False, events=[],
                  started_at=datetime.now(timezone.utc).isoformat())
    serial = bytearray()
    pending = bytearray()
    process = None
    with tempfile.TemporaryDirectory(prefix='fault-recover-') as tmp, \
            (out / 'stderr.log').open('wb') as err:
        path = str(Path(tmp) / 'qmp')
        command = [args.qemu, '-M', 'mps2-an505', '-kernel', str(args.elf),
                   '-display', 'none', '-monitor', 'none', '-serial', 'stdio',
                   '-nic', 'none', '-qmp', f'unix:{path},server=on,wait=off']
        result['command'] = command
        try:
            process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=err)
            with socket.socket(socket.AF_UNIX) as qmp:
                deadline = time.monotonic() + args.timeout
                while not Path(path).exists():
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('QEMU did not start')
                    time.sleep(.01)
                qmp.connect(path)
                qmp.sendall(b'{"execute":"qmp_capabilities"}\r\n')
                sent = checked = False
                start = 0
                while time.monotonic() < deadline:
                    ready, _, _ = select.select([process.stdout, qmp], [], [], .05)
                    if process.stdout in ready:
                        chunk = process.stdout.read1(65536)
                        if not chunk:
                            raise RuntimeError('QEMU exited before recovery')
                        serial.extend(chunk)
                    if qmp in ready:
                        chunk = qmp.recv(65536)
                        if chunk:
                            pending.extend(chunk)
                            while b'\n' in pending:
                                line, _, rest = pending.partition(b'\n')
                                pending[:] = rest
                                event = json.loads(line)
                                if 'event' in event and sent:
                                    result['events'].append(event)
                    if not sent and b'TASH>>' in serial:
                        start = len(serial)
                        result['trigger_at'] = time.monotonic()
                        process.stdin.write(f'faultlab {case}\n'.encode())
                        process.stdin.flush()
                        sent = True
                        deadline = time.monotonic() + args.timeout
                    body = bytes(serial[start:]) if sent else b''
                    reset = any(e['event'] == 'RESET' and
                                e.get('data', {}).get('guest') is True
                                for e in result['events'])
                    if sent and not checked and b'TASH>>' in body:
                        if case <= 3:
                            valid = (b'FAULTLAB: returned outer=1 nested=1' in body
                                     and b'FAULTLAB: scheduler progressed' in body
                                     and not reset)
                        else:
                            record_match = re.search(rb'FAULTREC valid=1 ([^\r\n]+)', body)
                            record = dict(re.findall(rb'(\w+)=([0-9a-f]+)', record_match[1])) if record_match else {}
                            result['record'] = {k.decode(): v.decode() for k, v in record.items()}
                            valid = (reset and record.get(b'first') == b'6'
                                     and record.get(b'frame_valid') == b'1'
                                     and int(record.get(b'pc', b'0'), 16) == args.trigger_pc
                                     and int(record.get(b'cfsr', b'0'), 16) & 0x10000 != 0)
                            if case == 5:
                                valid &= b'secondary=3' in body and b'injection=5' in body
                                match = re.search(rb'secondary_msp=([0-9a-f]+) secondary_cfsr=([0-9a-f]+)', body)
                                valid &= bool(match and 0x5fffffe0 <= int(match[1], 16) <= 0x60000000
                                              and int(match[2], 16) & 0x1000
                                              and b'secondary_frame_valid=0' in body)
                            elif case == 6:
                                valid &= b'uart_timeout=1' in body and b'injection=6' in body
                            else:
                                valid &= b'first=6' in body
                        if not valid:
                            raise RuntimeError('prompt returned without required recovery evidence')
                        result['recovery_seconds'] = time.monotonic() - result['trigger_at']
                        result['reset_observed'] = reset
                        process.stdin.write(b'faultlab\n')
                        process.stdin.flush()
                        checked = True
                        start = len(serial)
                    elif checked and b'usage: faultlab <1..6>' in body and b'TASH>>' in body:
                        result['passed'] = True
                        break
                if not result['passed']:
                    raise RuntimeError('timed out without recovery')
        except (OSError, RuntimeError, ValueError) as exc:
            result['error'] = str(exc)
        finally:
            result.pop('trigger_at', None)
            if process:
                result['qemu_returncode_before_cleanup'] = process.poll()
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
    parser.add_argument('--cases', default='1,2,3,4,5,6')
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=8)
    args = parser.parse_args()
    args.elf = args.elf.resolve()
    args.trigger_pc = Elf(args.elf).address('qemu_fault_lab_trigger')
    if args.repeat < 1 or args.timeout <= 0:
        parser.error('repeat and timeout must be positive')
    args.out.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(args.config, args.out / 'effective.config')
    metadata = dict(elf_sha256=hashlib.sha256(args.elf.read_bytes()).hexdigest(),
                    config_sha256=hashlib.sha256(args.config.read_bytes()).hexdigest(),
                    qemu=subprocess.check_output([args.qemu, '--version'], text=True).splitlines()[0],
                    head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip())
    sources = ['os/arch/arm/src/armv8-m/up_exception.S',
               'os/arch/arm/src/armv8-m/up_doirq.c',
               'os/arch/arm/src/armv8-m/up_fault_entry.S',
               'os/arch/arm/src/armv8-m/up_fault_recovery.c',
               'os/arch/arm/src/armv8-m/up_vectors.c',
               'os/arch/arm/src/qemu-armv8m/qemu_armv8m_serial.c',
               'os/board/qemu-armv8m/src/qemu_armv8m_fault_lab.c',
               'build/configs/qemu-armv8m/scripts/mps2-an505.ld']
    metadata['working_source_sha256'] = {p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
                                          for p in sources if Path(p).exists()}
    metadata['source_note'] = 'Working files at invocation; use ELF hash and build evidence to establish provenance.'
    (args.out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    results = []
    for repeat in range(args.repeat):
        for case in map(int, args.cases.split(',')):
            if case not in range(1, 7):
                parser.error('case must be 1..6')
            result = run_case(args, case, args.out / f'{case}-{repeat + 1}')
            results.append(result)
            print(f"case={case} repeat={repeat + 1} passed={result['passed']} "
                  f"{result.get('error', '')}", flush=True)
    (args.out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
