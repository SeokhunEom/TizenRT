#!/usr/bin/env python3
"""Compile changed kernel translation units with real TizenRT ARM headers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='health-monitor-arm-') as directory:
    tmp = Path(directory)
    (tmp / 'tinyara').mkdir()
    (tmp / 'arch').mkdir()
    for path in (ROOT / 'os/arch/arm/include').iterdir():
        (tmp / 'arch' / path.name).symlink_to(path)
    (tmp / 'arch/chip').symlink_to(ROOT / 'os/arch/arm/include/tiva')
    config = (ROOT / 'build/configs/qemu/tc_16m/defconfig').read_text()
    config = config.replace('CONFIG_HEAPINFO_USER_GROUP=y',
                            '# CONFIG_HEAPINFO_USER_GROUP is not set')
    (tmp / '.config').write_text(config)
    subprocess.run(['cc', '-Ios/tools', 'os/tools/mkconfig.c',
                    'os/tools/cfgparser.c', 'os/tools/cfgdefine.c',
                    '-o', str(tmp / 'mkconfig')], cwd=ROOT, check=True)
    args = ['clang', '--target=arm-none-eabi', '-mcpu=cortex-m3', '-mthumb',
            '-std=gnu99', '-ffreestanding', '-D__KERNEL__', '-I'+str(tmp),
            '-Ios/include', '-Ios/kernel', '-Ios/arch/arm/src/armv7-m',
            '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']
    for enabled in (False, True):
        (tmp / '.config').write_text(config + ('\nCONFIG_HEALTH_MONITOR=y\n' if enabled else ''))
        with (tmp / 'tinyara/config.h').open('w') as header:
            subprocess.run([str(tmp / 'mkconfig'), str(tmp)], stdout=header, check=True)
        sources = ['sched/sched_processtimer.c', 'sched/sched_releasetcb.c']
        if enabled:
            sources.append('health_monitor/health_monitor.c')
        for source in sources:
            subprocess.run(args + ['-c', 'os/kernel/'+source, '-o',
                           str(tmp / (Path(source).name+'.o'))], cwd=ROOT, check=True)
    # Unsupported SMP must fail, even if Kconfig was bypassed manually.
    result = subprocess.run(args + ['-DCONFIG_SMP=1', '-fsyntax-only',
                            'os/kernel/health_monitor/health_monitor.c'],
                            cwd=ROOT, capture_output=True, text=True)
    assert result.returncode and 'Health Monitor core requires UP' in result.stderr
print('PASS: real ARM headers, enabled/disabled scheduler hooks, unsupported SMP guard')
