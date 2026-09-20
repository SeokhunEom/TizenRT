#!/usr/bin/env python3
"""Reproduce the two review findings against the fixed source, without edits."""
import argparse
import ast
import json
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
HEAD = 'cc00205afc56e938132403dc3d3aae65db8e8cb6'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=HERE.parents[3])
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root).decode().strip()
    if actual != HEAD:
        raise RuntimeError('Expected fixed review head: ' + HEAD)
    if args.out:
        args.out.mkdir()
        out = args.out.resolve()
    else:
        out = Path(tempfile.mkdtemp(prefix='health-monitor-review-repro-'))
    source = root / 'tools/qemu-armv8m-health-monitor/fault-message.py'
    tree = ast.parse(source.read_text())
    node = next(item for item in tree.body if isinstance(item, ast.ClassDef) and item.name == 'Remote')
    observations = {}
    for level in (0, 1):
        namespace = {}
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), 'exec', optimize=level), namespace)
        remote = object.__new__(namespace['Remote'])
        calls = []
        remote.request = lambda request: calls.append(request) or 'OK'
        remote.breakpoint(0x1234)
        observations[str(level)] = {'requests': calls}
    if observations['0']['requests'] != ['Z1,1234,2'] or observations['1']['requests']:
        raise RuntimeError('Assertion side-effect finding no longer reproduces')
    tests = root / 'os/kernel/health_monitor/tests'
    pm = (HERE / 'spec/pm_prep_repro.c').read_text()
    pm = pm.replace('#include "/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor/os/kernel/health_monitor/tests/pm_test.c"', '#include "pm_test.c"')
    (out / 'pm_prep_repro.c').write_text(pm)
    (out / 'pm_prep_watchdog_repro.c').write_text((HERE / 'spec/pm_prep_watchdog_repro.c').read_text())
    # Rename only the test entry point; retain the original MMIO/clock model.
    fixture = (tests / 'watchdog_test.c').read_text()
    if fixture.count('int main(void)') != 1:
        raise RuntimeError('Watchdog fixture entry point changed')
    (out / 'watchdog_fixture.inc').write_text(fixture.replace('int main(void)', 'int original_watchdog_test_main(void)'))
    common = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pthread',
              '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    includes = [tests / 'pm_include', tests / 'timer_include', tests / 'include', root / 'os/kernel', tests]
    flags = ['-I' + str(path) for path in includes] + ['-idirafter', str(root / 'os/include')]
    logs = []
    for name in ('pm_prep_repro', 'pm_prep_watchdog_repro'):
        extra = []
        if 'watchdog' in name:
            extra = ['-I' + str(tests / 'wdog_include'),
                     '-I' + str(root / 'os/board/rtl8730e/src/component/soc/amebad2/fwlib/include')]
        command = common + extra + flags + [str(out / (name + '.c')), '-o', str(out / name)]
        subprocess.run(command, cwd=root, check=True)
        logs.append(subprocess.check_output([str(out / name)], cwd=root).decode())
    if 'wall_now=325 os_now=200' not in logs[0] or 'reset=0 wdt_started=1' not in logs[1]:
        raise RuntimeError('PM preparation finding no longer reproduces')
    result = {'status': 'findings_reproduced', 'source_head': HEAD,
              'python_assert_operations': observations, 'pm_observations': logs,
              'physical_board_tested': False, 'qemu_launched': False}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    print('Evidence:', out)


if __name__ == '__main__':
    main()
