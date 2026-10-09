#!/usr/bin/env python3
"""Reproduce three distinct fault-stop outcomes on a freshly booted TizenRT ELF.

Only hardware breakpoints and reads are sent over GDB RSP. Fault injections
are explicit, opt-in guest code. A timeout alone never constitutes a pass.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET


class Elf:
    def __init__(self, path):
        self.data = path.read_bytes()
        assert self.data[:6] == b'\x7fELF\x01\x01', 'Expected ELF32 little endian'
        hdr = struct.unpack_from('<16sHHIIIIIHHHHHH', self.data)
        sections = [struct.unpack_from('<10I', self.data, hdr[6] + i * hdr[11])
                    for i in range(hdr[12])]
        self.symbols = {}
        for sh in sections:
            if sh[1] != 2:
                continue
            st = sections[sh[6]]
            strings = self.data[st[4]:st[4]+st[5]]
            for off in range(sh[4], sh[4]+sh[5], sh[9]):
                name, addr, size, info, other, index = struct.unpack_from('<IIIBBH', self.data, off)
                name = strings[name:].split(b'\0', 1)[0].decode()
                if name and index:
                    self.symbols[name] = {'address': addr & ~1 if info & 15 == 2 else addr,
                                          'size': size, 'function': info & 15 == 2}
        self.functions = [(v['address'], v['size'], k) for k, v in self.symbols.items() if v['function']]

    def address(self, name):
        return self.symbols[name]['address']

    def symbol(self, address):
        matches = [(a, n) for a, size, n in self.functions if a <= address < a + size]
        return f'{max(matches)[1]}+0x{address-max(matches)[0]:x}' if matches else hex(address)


class Remote:
    def __init__(self, conn, trace):
        self.conn, self.trace = conn, trace
        self.regs = {}

    def packet(self):
        while True:
            ch = self.conn.recv(1)
            if not ch:
                raise EOFError('debugger disconnected')
            if ch == b'$':
                break
        raw = bytearray()
        while True:
            ch = self.conn.recv(1)
            if not ch:
                raise EOFError('debugger disconnected')
            if ch == b'#':
                break
            raw += ch
        checksum = b''
        while len(checksum) < 2:
            checksum += self.conn.recv(2-len(checksum))
        assert int(checksum, 16) == sum(raw) & 255
        self.conn.sendall(b'+')
        data = bytearray()
        i = 0
        while i < len(raw):
            if raw[i] == ord('*'):
                i += 1
                data.extend(bytes([data[-1]]) * (raw[i]-29))
            elif raw[i] == ord('}'):
                i += 1
                data.append(raw[i] ^ 32)
            else:
                data.append(raw[i])
            i += 1
        result = data.decode()
        self.trace.write('< ' + result + '\n'); self.trace.flush()
        return result

    def send(self, request):
        raw = request.encode()
        self.trace.write('> ' + request + '\n'); self.trace.flush()
        self.conn.sendall(b'$' + raw + b'#' + f'{sum(raw)&255:02x}'.encode())
        assert self.conn.recv(1) == b'+'

    def request(self, request):
        self.send(request)
        return self.packet()

    def xml(self, annex):
        offset, text = 0, ''
        while True:
            reply = self.request(f'qXfer:features:read:{annex}:{offset:x},800')
            assert reply[0] in 'ml', reply
            text += reply[1:]
            if reply[0] == 'l':
                # QEMU's GDB XML uses xi:include without a namespace declaration.
                return ET.fromstring(text.replace('xi:include', 'include'))
            offset += len(reply[1:].encode())

    def discover_registers(self):
        number = 0
        def walk(tree):
            nonlocal number
            for node in tree:
                if node.tag.endswith('include'):
                    walk(self.xml(node.attrib['href']))
                elif node.tag == 'reg':
                    number = int(node.get('regnum', number))
                    self.regs[node.get('name').lower()] = number
                    number += 1
                else:
                    walk(node)
        walk(self.xml('target.xml'))

    def memory(self, addr, size):
        reply = self.request(f'm{addr:x},{size:x}')
        if reply.startswith('E'):
            raise RuntimeError(f'memory {addr:#x}: {reply}')
        result = bytes.fromhex(reply)
        assert len(result) == size
        return result

    def word(self, addr):
        return int.from_bytes(self.memory(addr, 4), 'little')

    def registers(self):
        result = {}
        wanted = {'r0', 'r1', 'r2', 'r3', 'sp', 'lr', 'pc', 'xpsr', 'msp', 'psp',
                  'primask', 'basepri', 'faultmask', 'control', 'msp_ns', 'psp_ns',
                  'msp_s', 'psp_s'}
        for name, number in self.regs.items():
            if name in wanted:
                reply = self.request(f'p{number:x}')
                if reply and not reply.startswith('E') and 'x' not in reply:
                    result[name] = int.from_bytes(bytes.fromhex(reply), 'little')
        return result

    def breakpoint(self, addr, enabled=True):
        assert self.request(f'{"Z" if enabled else "z"}1,{addr:x},2') == 'OK'

    def interrupt(self):
        self.trace.write('> INTERRUPT\n'); self.trace.flush()
        self.conn.sendall(b'\x03')
        return self.packet()


FAULT_REGS = {'cfsr': 0xe000ed28, 'hfsr': 0xe000ed2c, 'shcsr': 0xe000ed24,
              'mmfar': 0xe000ed34, 'bfar': 0xe000ed38, 'icsr': 0xe000ed04,
              'uart_state': 0x40200004, 'uart_control': 0x40200008}


def run_case(args, elf, case, out):
    out.mkdir(parents=True)
    result = {'case': case, 'status': 'fail', 'events': [], 'samples': [],
              'started_at_utc': datetime.now(timezone.utc).isoformat(),
              'target_memory_or_register_writes_via_debugger': False,
              'historical_patch_absence_reproduced': False}
    proc = conn = None
    with tempfile.TemporaryDirectory(prefix='faultlab-', dir='/private/tmp') as tmp, \
         (out/'serial.log').open('wb') as serial, (out/'stderr.log').open('wb') as stderr, \
         (out/'rsp.log').open('w') as trace:
        sock_path = str(Path(tmp)/'gdb.sock')
        cmd = [args.qemu, '-M', 'mps2-an505', '-kernel', str(args.elf),
               '-display', 'none', '-monitor', 'none', '-serial', 'stdio', '-nic', 'none',
               '-S', '-gdb', f'unix:{sock_path},server=on,wait=off',
               '-d', 'int,guest_errors', '-D', str(out/'exceptions.log')]
        result['command'] = cmd
        try:
            proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=serial, stderr=stderr)
            deadline = time.monotonic() + args.timeout
            while not Path(sock_path).exists():
                if proc.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('QEMU failed to start')
                time.sleep(.05)
            conn = socket.socket(socket.AF_UNIX)
            conn.settimeout(5)
            conn.connect(sock_path)
            remote = Remote(conn, trace)
            remote.request('?')
            remote.discover_registers()
            result['register_map'] = remote.regs
            checkpoint = elf.address('qemu_fault_lab_checkpoint')
            breakpoints = {checkpoint: 'checkpoint', elf.address('up_usagefault'): 'usagefault_entry',
                           elf.address('up_hardfault'): 'hardfault_entry',
                           elf.address('up_assert'): 'assert_entry'}
            for addr in breakpoints:
                remote.breakpoint(addr)

            def snapshot(kind):
                regs = remote.registers()
                pc = regs['pc']
                entry = {'kind': kind, 'pc_symbol': elf.symbol(pc),
                         'registers': {k: hex(v) for k, v in regs.items()},
                         'fault_registers': {k: hex(remote.word(a)) for k, a in FAULT_REGS.items()},
                         'stage': remote.word(elf.address('g_qemu_fault_lab_stage'))}
                if kind == 'usagefault_entry':
                    # Save raw context as well as register values; decode indices from
                    # the exact source/config when interpreting the evidence.
                    entry['context_address'] = hex(regs['r1'])
                    entry['context_words'] = [hex(v) for v in struct.unpack(
                        f'<{args.context_words}I', remote.memory(regs['r1'], args.context_words * 4))]
                    entry['saved_exc_return'] = entry['context_words'][10]
                    entry['saved_fault_pc'] = entry['context_words'][args.context_words - 2]
                    entry['saved_xpsr'] = entry['context_words'][args.context_words - 1]
                return entry

            remote.send('c')
            while b'TASH>>' not in (out/'serial.log').read_bytes():
                if proc.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('fresh TASH prompt not reached')
                if select.select([conn], [], [], .05)[0]:
                    raise RuntimeError('unexpected stop before command: '+remote.packet())
            proc.stdin.write(f'faultlab {case}\n'.encode()); proc.stdin.flush()
            observed = False
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    break
                if select.select([conn], [], [], .1)[0]:
                    stop = remote.packet()
                    if not stop.startswith(('T', 'S')):
                        raise RuntimeError('unexpected stop '+stop)
                    regs = remote.registers()
                    addr = regs['pc']
                    kind = breakpoints.get(addr)
                    if kind is None:
                        raise RuntimeError('unplanned stop '+elf.symbol(addr))
                    entry = snapshot(kind)
                    result['events'].append(entry)
                    if case == 'panic' and kind == 'assert_entry':
                        observed = True
                    if case in ('uart', 'lockup') and kind == 'checkpoint' and entry['stage'] == 3:
                        observed = True
                    remote.breakpoint(addr, False)
                    if addr == checkpoint:
                        remote.request('s')
                        remote.breakpoint(addr)
                    else:
                        del breakpoints[addr]
                    remote.send('c')
                    if observed:
                        break
            if not observed:
                raise RuntimeError('required fault/injection milestone not reached')

            if case == 'lockup':
                proc.wait(timeout=min(5, args.timeout))
                stderr.flush()
                fatal = (out/'stderr.log').read_text(errors='replace')
                events = (out/'exceptions.log').read_text(errors='replace')
                result['qemu_returncode'] = proc.returncode
                final_regs = {f'r{int(n)}': hex(int(v, 16))
                              for n, v in re.findall(r'R(\d{2})=([0-9a-fA-F]{8})', fatal)}
                result['lockup_registers'] = final_regs
                result['lockup_pc_symbol'] = elf.symbol(int(final_regs['r15'], 16))
                result['lockup_reported'] = 'Lockup: can\'t escalate' in fatal
                result['hardfault_exception_seen'] = 'exception 3' in events
                result['stacking_busfault_seen'] = 'BFSR.STKERR' in events
                result['precise_busfault_seen'] = 'CFSR.PRECISERR' in events
                assert result['lockup_reported'], 'QEMU did not report architectural lockup'
                assert result['hardfault_exception_seen'], 'HardFault not observed'
                assert result['stacking_busfault_seen'] and result['precise_busfault_seen'], 'bad-stack faults not observed'
            else:
                # Let the guest reach its terminal loop, then take independently
                # resumed samples. Debugger stops themselves are not the hang.
                for sample in range(3):
                    time.sleep(.3)
                    remote.interrupt()
                    result['samples'].append(snapshot(f'sample_{sample}'))
                    if sample < 2:
                        remote.send('c')
                if case == 'panic':
                    assert all(s['pc_symbol'].startswith(('_up_assert', 'up_assert')) for s in result['samples']), 'not stopped in assert policy'
                    # A self branch, verified by disassembly, is stronger than a timeout.
                    result['terminal_instruction'] = remote.memory(int(result['samples'][-1]['registers']['pc'], 16), 4).hex()
                    assert result['terminal_instruction'].startswith('fee7'), 'expected Thumb b . halt loop'
                else:
                    assert all(s['pc_symbol'].startswith(('qemu_armv8m_lowputc', 'up_lowputc', 'up_putc')) for s in result['samples']), 'not in UART output loop'
                    assert all(int(s['fault_registers']['uart_state'], 16) & 1 for s in result['samples']), 'TXFULL not set'
                    assert all(not int(s['fault_registers']['uart_control'], 16) & 1 for s in result['samples']), 'TX unexpectedly enabled'
                    assert all(int(s['fault_registers']['hfsr'], 16) == 0 for s in result['samples']), 'unexpected HardFault escalation'
            assert any(e['kind'] == 'usagefault_entry' and
                       int(e['fault_registers']['cfsr'], 16) & 0x10000 and
                       int(e['registers']['xpsr'], 16) & 0x1ff == 6 and
                       int(e['saved_fault_pc'], 16) == elf.address('qemu_fault_lab_trigger')
                       for e in result['events']), 'real UDF UsageFault entry not observed'
            result['status'] = 'pass'
        except Exception as exc:
            result['error'] = f'{type(exc).__name__}: {exc}'
        finally:
            if conn:
                conn.close()
            if proc and proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill(); proc.wait()
            result['qemu_returncode_after_cleanup'] = proc.returncode if proc else None
            result['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
            (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[2]
    parser.add_argument('--elf', type=Path, default=root/'build/output/bin/tinyara')
    parser.add_argument('--config', type=Path, default=root/'os/.config')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--case', choices=['all', 'panic', 'lockup', 'uart'], default='all')
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--qemu', default=shutil.which('qemu-system-arm'))
    args = parser.parse_args()
    args.elf, args.output = args.elf.resolve(), args.output.resolve()
    assert args.qemu and args.repeat > 0
    cfg = args.config.read_text()
    assert 'CONFIG_QEMU_FAULT_LAB=y' in cfg
    assert 'CONFIG_BOARD_ASSERT_AUTORESET=y' not in cfg
    assert 'CONFIG_ARCH_FPU=y' not in cfg
    args.context_words = 21 if 'CONFIG_REG_STACK_OVERFLOW_PROTECTION=y' in cfg else 20
    args.output.mkdir(parents=True, exist_ok=False)
    elf = Elf(args.elf)
    shutil.copy2(args.config, args.output/'effective.config')
    shutil.copy2(args.elf, args.output/'tinyara')
    result = {'elf_sha256': hashlib.sha256(elf.data).hexdigest(),
              'config_sha256': hashlib.sha256(args.config.read_bytes()).hexdigest(),
              'qemu_version': subprocess.check_output([args.qemu, '--version'], text=True).splitlines()[0],
              'git_head': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
              'scope': 'controlled outcome reproduction; not the historical missing-patch race', 'runs': []}
    source_paths = [Path(__file__).relative_to(root),
                    Path('os/board/qemu-armv8m/src/qemu_armv8m_fault_lab.c'),
                    Path('os/board/qemu-armv8m/src/qemu_armv8m_boot.c'),
                    Path('os/board/qemu-armv8m/src/Makefile'),
                    Path('os/arch/arm/src/qemu-armv8m/Kconfig'),
                    Path('os/arch/arm/src/armv8-m/up_usagefault.c'),
                    Path('os/arch/arm/src/armv8-m/up_exception.S'),
                    Path('os/arch/arm/include/armv8-m/irq_cmnvector.h'),
                    Path('build/configs/qemu-armv8m/fault_lab/defconfig')]
    result['source_sha256'] = {}
    for path in source_paths:
        destination = args.output/'sources'/path
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root/path, destination)
        result['source_sha256'][str(path)] = hashlib.sha256((root/path).read_bytes()).hexdigest()
    (args.output/'source.diff').write_bytes(subprocess.check_output(['git', '-C', str(root), 'diff', 'HEAD']))
    cases = ['panic', 'lockup', 'uart'] if args.case == 'all' else [args.case]
    for repeat in range(args.repeat):
        for case in cases:
            run = run_case(args, elf, case, args.output/f'{case}-{repeat+1}')
            result['runs'].append(run)
            print(case, repeat+1, run['status'], run.get('error', ''), flush=True)
    result['status'] = 'pass' if all(r['status'] == 'pass' for r in result['runs']) else 'fail'
    (args.output/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    return 0 if result['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
