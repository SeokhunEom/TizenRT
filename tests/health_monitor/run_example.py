#!/usr/bin/env python3
"""Example CLI integration with the production core; no target fault injection."""
from pathlib import Path
import subprocess
import tempfile
from run import HEADERS, ROOT

with tempfile.TemporaryDirectory(prefix='health-monitor-example-') as directory:
    tmp = Path(directory)
    for name, contents in HEADERS.items():
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)
    (tmp / 'tinyara/health_monitor.h').write_text((ROOT / 'os/include/tinyara/health_monitor.h').read_text())
    for smp in (False, True):
        command = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-I'+str(tmp),
                   str(Path(__file__).with_name('test_example.c')), '-o', str(tmp / 'example')]
        subprocess.run(command + (['-DTEST_SMP=1'] if smp else []), check=True)
        result = subprocess.run([str(tmp / 'example')], capture_output=True, text=True)
        if result.returncode:
            print(result.stdout, result.stderr)
            result.check_returncode()
        assert 'PASS example:' in result.stdout
        print(('SMP ' if smp else 'UP ') + result.stdout.splitlines()[-1])
    # Use the actual example Makefile and repository REGISTER macro. Route all
    # generated registry files into the temporary directory, never apps/.
    registry = tmp / 'registry'
    registry.mkdir()
    (tmp / '.config').write_text('CONFIG_BUILTIN_APPS=y\nCONFIG_EXAMPLES_HEALTH_MONITOR=y\n')
    subprocess.run(['make', '-s', 'context', 'TOPDIR='+str(tmp), 'APPDIR='+str(ROOT / 'apps'),
                    'BUILTIN_REGISTRY='+str(registry), 'DELIM=/'],
                   cwd=ROOT / 'apps/examples/health_monitor', check=True)
    entry = (registry / 'health_monitor_main.mdat').read_text()
    assert '"health_monitor"' in entry and 'health_monitor_main' in entry and 'TASH_EXECMD_ASYNC' in entry
    assert 'health_monitor_main' in (registry / 'health_monitor_main.pdat').read_text()
    print('PASS: actual Makefile registers health_monitor as a separate asynchronous TASH task')
