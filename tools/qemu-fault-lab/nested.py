#!/usr/bin/env python3
"""Exercise nested IRQ entry windows with the 2022 stack fix removed.

The guest uses two real NVIC external interrupts. The QEMU-only assembly hook
can pend the higher-priority IRQ at exception_common's first entry window;
it does not alter stack pointers or fault registers. A second case pends it
from the outer ISR after the normal interrupt stack is active.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import select
import shutil
import socket
import subprocess
import tempfile
import time

from run import Elf, Remote, FAULT_REGS


def read_word(remote, address):
    return remote.word(address)


def run_case(args, elf, case, out):
    out.mkdir(parents=True)
    result = {
        'case': case,
        'status': 'fail',
        'events': [],
        'started_at_utc': datetime.now(timezone.utc).isoformat(),
        'guest_fault_register_writes': False,
        'guest_sets_psp_and_msp_to_the_same_valid_stack_value': True,
        'debugger_target_writes': False,
        'scenario': 'controlled NVIC scheduling; no invalid stack or fault injection',
        'usagefault_handler_entered': False,
        'hardfault_handler_entered': False,
        'assert_handler_entered': False,
    }
    proc = conn = None
    with tempfile.TemporaryDirectory(prefix='nestedirq-', dir='/private/tmp') as tmp, \
         (out/'serial.log').open('wb') as serial, \
         (out/'stderr.log').open('wb') as stderr, \
         (out/'rsp.log').open('w') as trace:
        sock_path = str(Path(tmp)/'gdb.sock')
        command = [args.qemu, '-M', 'mps2-an505', '-kernel', str(args.elf),
                   '-display', 'none', '-monitor', 'none', '-serial', 'stdio',
                   '-nic', 'none', '-S', '-gdb', f'unix:{sock_path},server=on,wait=off',
                   '-d', 'int,guest_errors', '-D', str(out/'exceptions.log')]
        result['command'] = command
        try:
            proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=serial, stderr=stderr)
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

            launch = elf.address('qemu_fault_lab_nested_launch')
            symbols = {
                elf.address('qemu_fault_lab_irq_window'): 'outer_irq_window',
                elf.address('qemu_fault_lab_nested_irq_window'): 'nested_irq_window',
                elf.address('up_usagefault'): 'usagefault',
                elf.address('up_hardfault'): 'hardfault',
                elf.address('up_assert'): 'assert',
            }
            spin = elf.address('qemu_fault_lab_nested_spin')
            remote.breakpoint(launch)
            remote.send('c')
            while b'TASH>>' not in (out/'serial.log').read_bytes():
                if proc.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('fresh TASH prompt not reached')
                if select.select([conn], [], [], .05)[0]:
                    raise RuntimeError('unexpected stop before TASH: ' + remote.packet())
            selection = {'early40': 1, 'early00': 2, 'late40': 3}[case]
            proc.stdin.write(f'faultlab {selection}\n'.encode())
            proc.stdin.flush()
            if not select.select([conn], [], [], max(0, deadline-time.monotonic()))[0]:
                raise RuntimeError('faultlab nested command did not reach its launch breakpoint')
            launch_stop = remote.packet()
            if not launch_stop.startswith(('T', 'S')):
                raise RuntimeError('unexpected stop at nested launch: ' + launch_stop)
            launch_regs = remote.registers()
            if launch_regs.get('pc') != launch:
                raise RuntimeError('nested launch breakpoint landed at ' +
                                   elf.symbol(launch_regs.get('pc', 0)))
            result['launch_registers'] = {name: hex(value) for name, value in launch_regs.items()}
            result['nvic_before_outer_pend'] = {
                'enable_64_95': hex(remote.word(0xe000e108)),
                'pending_64_95': hex(remote.word(0xe000e208)),
                'priority_68_71': hex(remote.word(0xe000e444)),
                'active_64_95': hex(remote.word(0xe000e308)),
            }
            remote.breakpoint(launch, False)
            for address in symbols:
                remote.breakpoint(address)
            active_breakpoints = set(symbols)
            remote.send('c')

            outer_seen = False
            nested_seen = False
            terminal_seen = False
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    break
                if not select.select([conn], [], [], .1)[0]:
                    continue
                stop = remote.packet()
                if not stop.startswith(('T', 'S')):
                    raise RuntimeError('unexpected GDB stop: ' + stop)
                regs = remote.registers()
                pc = regs.get('pc', 0)
                kind = symbols.get(pc)
                if kind is None:
                    raise RuntimeError(f'unplanned stop at {elf.symbol(pc)}')
                if kind in ('outer_irq_window', 'nested_irq_window'):
                    vector = regs.get('xpsr', 0) & 0x1ff
                    if kind == 'outer_irq_window':
                        if vector != 86:
                            raise RuntimeError(f'outer-window breakpoint hit vector {vector}')
                        outer_seen = True
                        label = 'outer_irq_entry_window'
                    else:
                        if vector != 87:
                            raise RuntimeError(f'nested-window breakpoint hit vector {vector}')
                        nested_seen = True
                        label = 'nested_irq_entry_window'
                elif kind == 'spin':
                    terminal_seen = True
                    label = 'returned_to_thread_spin'
                else:
                    label = kind

                snapshot = {
                    'kind': label,
                    'pc_symbol': elf.symbol(pc),
                    'registers': {name: hex(value) for name, value in regs.items()},
                    'fault_registers': {name: hex(remote.word(address))
                                        for name, address in FAULT_REGS.items()
                                        if name in ('cfsr', 'hfsr', 'shcsr', 'icsr')},
                    'stage': hex(read_word(remote, elf.address('g_qemu_fault_lab_stage'))),
                    'outer_irq_count': read_word(remote, elf.address('g_qemu_fault_lab_outer_irq_count')),
                    'nested_irq_count': read_word(remote, elf.address('g_qemu_fault_lab_nested_irq_count')),
                    'early_pending_flag': read_word(remote, elf.address('g_qemu_fault_lab_irq_pending')),
                    'nvic_enable_64_95': hex(remote.word(0xe000e108)),
                    'nvic_pending_64_95': hex(remote.word(0xe000e208)),
                    'nvic_active_64_95': hex(remote.word(0xe000e308)),
                }
                result['events'].append(snapshot)
                if kind == 'usagefault':
                    result['usagefault_handler_entered'] = True
                    remote.breakpoint(pc, False)
                    active_breakpoints.discard(pc)
                elif kind == 'hardfault':
                    result['hardfault_handler_entered'] = True
                    remote.breakpoint(pc, False)
                    active_breakpoints.discard(pc)

                if kind == 'outer_irq_window':
                    remote.breakpoint(pc, False)
                    active_breakpoints.discard(pc)
                if kind == 'nested_irq_window':
                    remote.breakpoint(pc, False)
                    active_breakpoints.discard(pc)
                    if spin not in symbols:
                        remote.breakpoint(spin)
                        symbols[spin] = 'spin'
                        active_breakpoints.add(spin)
                if kind == 'assert':
                    # up_assert is a terminal policy. Continue briefly to verify
                    # that the handler remains there rather than treating the
                    # debugger breakpoint itself as a halt.
                    if kind == 'assert':
                        result['assert_handler_entered'] = True
                    for address in active_breakpoints:
                        remote.breakpoint(address, False)
                    active_breakpoints.clear()
                    if kind == 'assert':
                        remote.send('c')
                        time.sleep(.2)
                        if proc.poll() is None:
                            remote.interrupt()
                            post = remote.registers()
                            result['post_assert_sample'] = {
                                'pc': hex(post.get('pc', 0)),
                                'pc_symbol': elf.symbol(post.get('pc', 0)),
                                'xpsr': hex(post.get('xpsr', 0)),
                            }
                    break
                if kind == 'spin':
                    for address in active_breakpoints:
                        remote.breakpoint(address, False)
                    active_breakpoints.clear()
                    break
                remote.send('c')

            stderr.flush()
            exception_log = (out/'exceptions.log').read_text(errors='replace')
            fatal_log = (out/'stderr.log').read_text(errors='replace')
            result['qemu_returncode'] = proc.poll()
            result['qemu_lockup_reported'] = "Lockup: can't escalate" in fatal_log
            result['hardfault_exception_seen'] = 'exception 3' in exception_log
            result['stacking_busfault_seen'] = 'BFSR.STKERR' in exception_log
            result['precise_busfault_seen'] = 'CFSR.PRECISERR' in exception_log
            result['outer_irq_entry_seen'] = outer_seen
            result['nested_irq_entry_seen'] = nested_seen
            result['returned_to_thread_spin'] = terminal_seen
            if (result['usagefault_handler_entered'] or result['hardfault_handler_entered']) and \
                    not result['assert_handler_entered'] and not result['qemu_lockup_reported'] and \
                    proc and proc.poll() is None:
                samples = []
                for sample_index in range(2):
                    remote.interrupt()
                    regs = remote.registers()
                    samples.append({'pc': hex(regs.get('pc', 0)),
                                    'pc_symbol': elf.symbol(regs.get('pc', 0)),
                                    'xpsr': hex(regs.get('xpsr', 0))})
                    if sample_index == 0:
                        remote.send('c')
                        time.sleep(.2)
                result['post_fault_handler_samples'] = samples
            if result['qemu_lockup_reported']:
                result['status'] = 'lockup'
            elif result['assert_handler_entered']:
                result['status'] = 'panic-halt'
            elif result.get('post_fault_handler_samples') and \
                    result['post_fault_handler_samples'][0]['pc_symbol'] == \
                    result['post_fault_handler_samples'][1]['pc_symbol']:
                result['status'] = 'fault-handler-stalled'
            elif result['hardfault_handler_entered']:
                result['status'] = 'hardfault-handler-running'
            elif result['usagefault_handler_entered']:
                result['status'] = 'usagefault-handler-stalled'
            elif terminal_seen and nested_seen:
                last = result['events'][-1]
                result['status'] = ('nested-completed'
                                    if last['outer_irq_count'] and last['nested_irq_count']
                                    else 'nested-returned-with-bad-counters')
            elif outer_seen and not nested_seen:
                result['status'] = 'nested-not-taken'
            else:
                result['status'] = 'stalled'
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
                    proc.kill()
                    proc.wait()
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
    parser.add_argument('--case', choices=['early40', 'early00', 'late40', 'all'], default='all')
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=15)
    parser.add_argument('--qemu', default=shutil.which('qemu-system-arm'))
    parser.add_argument('--patch-state', choices=['patch-removed', 'patch-present'],
                        default='patch-removed')
    args = parser.parse_args()
    args.elf = args.elf.resolve()
    args.output = args.output.resolve()
    assert args.qemu and args.repeat > 0
    config_text = args.config.read_text()
    assert 'CONFIG_QEMU_FAULT_LAB=y' in config_text
    assert 'CONFIG_ARCH_NESTED_INTERRUPT=y' in config_text
    assert 'CONFIG_ARCH_INTERRUPTSTACK=2048' in config_text
    args.output.mkdir(parents=True, exist_ok=False)
    elf = Elf(args.elf)
    shutil.copy2(args.config, args.output/'effective.config')
    shutil.copy2(args.elf, args.output/'tinyara')
    result = {
        'elf_sha256': hashlib.sha256(elf.data).hexdigest(),
        'config_sha256': hashlib.sha256(args.config.read_bytes()).hexdigest(),
        'qemu_version': subprocess.check_output([args.qemu, '--version'], text=True).splitlines()[0],
        'git_head': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
        'patch_state': args.patch_state,
        'scope': 'controlled nested IRQ schedule matrix; never forces an invalid MSP or writes fault state',
        'runs': [],
    }
    cases = ['early40', 'early00', 'late40'] if args.case == 'all' else [args.case]
    for repeat in range(args.repeat):
        for case in cases:
            run = run_case(args, elf, case, args.output/f'{case}-{repeat+1}')
            result['runs'].append(run)
            print(case, repeat + 1, run['status'], run.get('error', ''), flush=True)
    result['status'] = 'pass' if all(r['status'] in
                                    ('nested-completed', 'usagefault-handler-stalled',
                                     'hardfault-handler-running', 'fault-handler-stalled',
                                     'panic-halt', 'lockup')
                                    for r in result['runs']) else 'fail'
    (args.output/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    return 0 if result['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
