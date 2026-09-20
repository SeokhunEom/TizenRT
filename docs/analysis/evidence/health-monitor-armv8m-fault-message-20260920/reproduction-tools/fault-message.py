#!/usr/bin/env python3
"""Observe a real user CPU fault through MQ delivery, stopping before recovery.

Requires fault-debug-metadata.json extracted from this exact kernel ELF by GDB.
Hardware execution breakpoints only: no target code, register or memory writes.
"""
import argparse
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
import sys
import time
import traceback


class Remote:
    def __init__(self, connection, trace):
        self.socket = connection
        self.trace = trace

    def packet(self):
        while True:
            first = self.socket.recv(1)
            if not first:
                raise EOFError('QEMU debugger disconnected')
            if first == b'$':
                break
        raw = bytearray()
        while True:
            ch = self.socket.recv(1)
            if not ch:
                raise EOFError('QEMU debugger disconnected')
            if ch == b'#':
                break
            raw.extend(ch)
        checksum = self.socket.recv(2)
        assert int(checksum, 16) == sum(raw) & 255
        self.socket.sendall(b'+')
        # GDB RSP replies may use run-length encoding.
        data = bytearray()
        i = 0
        while i < len(raw):
            if raw[i] == ord('*'):
                i += 1
                data.extend(bytes([data[-1]]) * (raw[i] - 29))
            elif raw[i] == ord('}'):
                i += 1
                data.append(raw[i] ^ 0x20)
            else:
                data.append(raw[i])
            i += 1
        text = data.decode()
        self.trace.write('< ' + text + '\n'); self.trace.flush()
        return text

    def send(self, text):
        data = text.encode()
        self.trace.write('> ' + text + '\n'); self.trace.flush()
        self.socket.sendall(b'$' + data + b'#' + ('%02x' % (sum(data) & 255)).encode())
        assert self.socket.recv(1) == b'+'

    def request(self, text):
        self.send(text)
        return self.packet()

    def memory(self, address, size):
        data = bytes.fromhex(self.request(f'm{address:x},{size:x}'))
        assert len(data) == size
        return data

    def word(self, address):
        return int.from_bytes(self.memory(address, 4), 'little')

    def registers(self):
        data = bytes.fromhex(self.request('g'))
        return struct.unpack('<16I', data[:64])

    def breakpoint(self, address, enabled=True):
        assert self.request(('Z' if enabled else 'z') + f'1,{address:x},2') == 'OK'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--profile', choices=['loadable_all', 'loadable_apps', 'xip_all'], required=True)
    p.add_argument('--app', choices=['app1', 'app2'], required=True)
    p.add_argument('--kind', choices=['udf', 'mpu'], required=True)
    p.add_argument('--unregistered', action='store_true')
    a = p.parse_args()
    a.output.mkdir()
    meta = json.loads((a.build / 'fault-debug-metadata.json').read_text())
    config = (a.build / 'effective.config').read_text()
    assert meta['elf_sha256'] == hashlib.sha256((a.build / 'bin/tinyara').read_bytes()).hexdigest()
    assert meta['config_sha256'] == hashlib.sha256((a.build / 'effective.config').read_bytes()).hexdigest()
    assert 'CONFIG_BINMGR_RECOVERY=y' in config and 'CONFIG_BINMGR_RELOAD_REBOOT=y' not in config
    assert 'CONFIG_EXAMPLES_HEALTH_MONITOR_ARMV8M=y' in config
    assert a.app == 'app1' or a.profile != 'xip_all'
    sys.path.insert(0, str(a.root / '.github/scripts'))
    from qemu_armv8m_ab import stage_state, extract_active_kernel, qemu_command
    # Bind the authoritative A/B layout to immutable saved build artifacts.
    mirror = a.output / 'saved-build-root'
    (mirror / 'os').mkdir(parents=True)
    shutil.copy2(a.build / 'effective.config', mirror / 'os/.config')
    (mirror / 'build/output').mkdir(parents=True)
    (mirror / 'build/output/bin').symlink_to(a.build.resolve() / 'bin', target_is_directory=True)
    state = a.output / 'qemu.state'
    stage_state(mirror, a.profile, state)
    kernel = extract_active_kernel(mirror, a.profile, state)
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    command = qemu_command(state, kernel)
    command[0] = shutil.which('qemu-system-arm') or '/opt/homebrew/bin/qemu-system-arm'
    command += ['-nic', 'none', '-S', '-gdb', f'tcp:127.0.0.1:{port}']
    result = {'status': 'fail', 'profile': a.profile, 'app': a.app, 'kind': a.kind,
              'registered': not a.unregistered, 'firmware_sha256': hashlib.sha256((a.build / 'bin/tinyara').read_bytes()).hexdigest(),
              'qemu_command': command, 'events': [], 'recovery_executed': False,
              'unload_tested': False, 'target_memory_or_register_writes': False}
    process = None
    connection = None
    started = time.monotonic()
    serial = bytearray()
    with (a.output / 'serial.log').open('wb') as log, (a.output / 'gdb-rsp.log').open('w') as trace:
        def pump(timeout=0):
            if select.select([process.stdout], [], [], timeout)[0]:
                data = os.read(process.stdout.fileno(), 65536)
                if not data:
                    raise EOFError('QEMU exited before message receipt')
                serial.extend(data); log.write(data); log.flush()

        def event(label, regs, **extra):
            record = {'stage': label, 'pc': hex(regs[15]), 'registers': [hex(r) for r in regs], **extra}
            result['events'].append(record)
            return record

        def task():
            tcb = remote.word(meta['ready_list'])
            group = remote.word(tcb + meta['group_offset'])
            return {'tcb': hex(tcb), 'pid': int.from_bytes(remote.memory(tcb + meta['pid_offset'], meta['pid_size']), 'little', signed=True),
                    'binidx': remote.word(group + meta['binidx_offset']),
                    'health_timeout': remote.word(tcb + meta['health_timeout_offset']),
                    'health_deadline': remote.word(tcb + meta['health_deadline_offset'])}

        def message(address):
            raw = remote.memory(address, meta['message_size'])
            return {'address': hex(address), 'raw': raw.hex(),
                    'cmd': struct.unpack_from('<i', raw, meta['message_cmd'])[0],
                    'binidx': struct.unpack_from('<i', raw, meta['message_binidx'])[0]}

        try:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
            for _ in range(100):
                try:
                    connection = socket.create_connection(('127.0.0.1', port), timeout=5)
                    break
                except ConnectionRefusedError:
                    time.sleep(.05)
            if connection is None:
                raise RuntimeError('QEMU debugger unavailable')
            remote = Remote(connection, trace)
            remote.request('?')
            active = {meta[key] for key in ('usage_entry', 'mpu_entry', 'hard_entry', 'send_call', 'dispatch_call', 'recovery_entry')}
            for address in active:
                remote.breakpoint(address)
            remote.send('c')
            apps = ['app1'] if a.profile == 'xip_all' else ['app1', 'app2']
            deadline = time.monotonic() + 90
            while not (b'TASH>>' in serial and all(re.search(('HM_USER READY app=' + app + r' pid=\d+').encode(), serial) for app in apps)):
                pump(.05)
                if select.select([connection], [], [], 0)[0]:
                    raise RuntimeError('Unexpected debugger stop during boot: ' + remote.packet())
                if time.monotonic() > deadline:
                    raise TimeoutError('Protected app boot/readiness timeout')
            command_text = 'fault-' + a.kind + ('-empty' if a.unregistered else '')
            result['trigger'] = 'hm_armv8 send ' + a.app + ' ' + command_text
            process.stdin.write((result['trigger'] + '\n').encode()); process.stdin.flush()
            deadline = time.monotonic() + 30
            fault = sent = received = None
            while time.monotonic() < deadline:
                pump(.01)
                if not select.select([connection], [], [], 0)[0]:
                    if b'HM_USER FAIL' in serial or b'HM_CONTROL FAIL' in serial:
                        raise RuntimeError('Fault fixture failed')
                    continue
                stop = remote.packet()
                assert stop.startswith(('T05', 'S05')), stop
                regs = remote.registers(); pc = regs[15]
                if pc == meta['recovery_entry']:
                    raise RuntimeError('Recovery entry reached instead of the pre-call stop')
                if pc in (meta['usage_entry'], meta['mpu_entry'], meta['hard_entry']):
                    assert fault is None, 'Repeated CPU fault before message delivery'
                    expected = meta['usage_entry'] if a.kind == 'udf' else meta['mpu_entry']
                    assert pc in (expected, meta['hard_entry']), 'Wrong CPU exception'
                    actual = task()
                    assert actual['binidx'] == (1 if a.app == 'app1' else 2)
                    assert actual['health_timeout'] == (0 if a.unregistered else 60000)
                    fault_pc = remote.word(regs[1] + meta['context_pc_offset'])
                    fault = event('cpu_fault_handler', regs, irq=regs[0], task=actual,
                                  fault_pc=hex(fault_pc), instruction=remote.memory(fault_pc, 4).hex(),
                                  cfsr=hex(remote.word(0xe000ed28)), hfsr=hex(remote.word(0xe000ed2c)),
                                  mmfar=hex(remote.word(0xe000ed34)),
                                  tick=remote.word(meta['system_timer']))
                    assert regs[0] == (3 if pc == meta['hard_entry'] else (6 if a.kind == 'udf' else 4))
                    if pc == meta['hard_entry']:
                        assert int(fault['hfsr'], 16) & 0x40000000, 'Expected forced HardFault escalation'
                    if a.kind == 'udf':
                        assert int(fault['cfsr'], 16) & 0x10000 and fault['instruction'].startswith('00de')
                    else:
                        assert int(fault['cfsr'], 16) & 0x82 == 0x82 and fault['mmfar'] == '0x80000000'
                    remote.breakpoint(pc, False); active.remove(pc)
                    remote.breakpoint(meta['receive_return']); active.add(meta['receive_return'])
                elif pc == meta['send_call']:
                    assert fault is not None and sent is None
                    sent = event('fault_sender_before_mq_send', regs, message=message(regs[1]), task=task(),
                                 length=regs[2], priority=regs[3], queue_fd=hex(regs[0]))
                    assert sent['message']['cmd'] == 10 and sent['message']['binidx'] == fault['task']['binidx']
                    assert sent['length'] == meta['message_size'] and sent['priority'] == 100
                    assert int(sent['task']['tcb'], 16) == remote.word(meta['sender_tcb'])
                    remote.breakpoint(pc, False); active.remove(pc)
                elif pc == meta['receive_return']:
                    msg = message(regs[13] + meta['receive_buffer_sp_offset'])
                    if msg['cmd'] == 10:
                        assert sent is not None and regs[0] == meta['message_size']
                        assert msg['raw'] == sent['message']['raw']
                        received = event('binary_manager_after_mq_receive', regs, message=msg, length=regs[0], task=task())
                        remote.breakpoint(pc, False); active.remove(pc)
                    else:
                        # Avoid re-hitting an unrelated message's return instruction.
                        remote.breakpoint(pc, False)
                        remote.send('s'); remote.packet()
                        remote.breakpoint(pc)
                elif pc == meta['dispatch_call']:
                    assert fault is not None and sent is not None and received is not None
                    assert regs[0] == fault['task']['binidx']
                    event('stopped_before_binary_manager_recovery_call', regs, argument_binidx=regs[0], task=task())
                    result['stopped_before_recovery_call'] = True
                    break
                else:
                    raise RuntimeError('Unexpected breakpoint PC: ' + hex(pc))
                remote.send('c')
            else:
                raise TimeoutError('CPU fault did not reach the message receiver')
            pump(.1)
            text = serial.decode(errors='replace')
            notice = re.search(r'HM_USER CPU_FAULT app=' + a.app + r' pid=(\d+) kind=' + a.kind + r' control=(\d+) registered=(\d+) timeout_ms=(\d+)', text)
            assert notice and int(notice[1]) == fault['task']['pid'] and int(notice[2]) & 1
            assert int(notice[3]) == (not a.unregistered)
            expected_type = 'UNDEFINSTR' if a.kind == 'udf' else 'DACCVIOL'
            if fault['irq'] != 3:
                assert 'FAULT TYPE: ' + expected_type in text
            else:
                assert re.search(r'CFAULTS:\s*0*' + fault['cfsr'][2:] + r'\b', text)
            assert len(re.findall(r'Assertion failed at file:', text)) == 1
            assert ('up_hardfault.c' if fault['irq'] == 3 else ('up_usagefault.c' if a.kind == 'udf' else 'up_memfault.c')) in text
            assert re.search(r'Assert location \(PC\) : 0x0*' + fault['fault_pc'][2:], text)
            assert not re.search(r'health_monitor_timeout:|HEALTH MONITOR TIMEOUT|Try to recover fault|Failed to deactivate binary', text)
            result.update(status='pass', fault_notice=notice[0], fault_type=expected_type,
                          same_message_bytes_received=True, observed_message_size=meta['message_size'])
        except Exception as error:
            result['error'] = repr(error)
            result['traceback'] = traceback.format_exc()
        finally:
            # Terminate while the vCPU is halted; never detach/resume into recovery.
            if process is not None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(timeout=5)
            if connection is not None:
                connection.close()
            result['elapsed_seconds'] = round(time.monotonic() - started, 3)
            (a.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('status', 'profile', 'app', 'kind', 'registered', 'elapsed_seconds')}))
    if result['status'] != 'pass':
        print(result.get('error', 'Unknown failure'), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
