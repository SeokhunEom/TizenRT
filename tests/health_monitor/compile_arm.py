#!/usr/bin/env python3
"""Real-header UP/SMP object build and production word-access assembly audit."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PORTS = [
    ('qemu/tc_16m', 'tiva', 'armv7-m', 'cortex-m3', ['-mthumb']),
    ('rtl8730e/loadable_ext_ddr_st7785', 'amebasmart', 'armv7-a', 'cortex-a32', []),
]
with tempfile.TemporaryDirectory(prefix='health-monitor-arm-') as directory:
    tmp = Path(directory)
    (tmp / 'tinyara').mkdir()
    (tmp / 'arch').mkdir()
    for path in (ROOT / 'os/arch/arm/include').iterdir():
        (tmp / 'arch' / path.name).symlink_to(path)
    subprocess.run(['cc', '-Ios/tools', 'os/tools/mkconfig.c',
                    'os/tools/cfgparser.c', 'os/tools/cfgdefine.c',
                    '-o', str(tmp / 'mkconfig')], cwd=ROOT, check=True)
    for board, chip, family, cpu, extra in PORTS:
        link = tmp / 'arch/chip'
        if link.is_symlink():
            link.unlink()
        link.symlink_to(ROOT / 'os/arch/arm/include' / chip)
        config = (ROOT / 'build/configs' / board / 'defconfig').read_text()
        # These integrations remain outside the core's supported scope.
        for symbol in ['HEAPINFO_USER_GROUP', 'APP_BINARY_SEPARATION',
                       'SCHED_TICKSUPPRESS', 'PM_TICKSUPPRESS']:
            config = config.replace('CONFIG_'+symbol+'=y', '# CONFIG_'+symbol+' is not set')
        args = ['clang', '--target=arm-none-eabi', '-mcpu='+cpu, *extra,
                '-std=gnu99', '-ffreestanding', '-D__KERNEL__', '-I'+str(tmp),
                '-Ios/include', '-Ios/kernel', '-Ios/arch/arm/src/'+family,
                '-Ios/arch/arm/src/'+chip,
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
        assembly = tmp / 'health_monitor.s'
        subprocess.run(args + ['-O2', '-S', 'os/kernel/health_monitor/health_monitor.c',
                               '-o', str(assembly)], cwd=ROOT, check=True)
        text = assembly.read_text()
        assert not re.search(r'\b(?:ldrex\w*|strex\w*|swp\w*)\b|__atomic|__sync|libatomic', text)
        if chip == 'amebasmart':
            assert re.search(r'\bdmb\s+sy', text)
        # Snapshot and API helpers may call integer arithmetic helpers, but
        # no waiting/allocation/VFS primitive may appear in their call graph.
        assert not re.search(r'\b(?:sem_wait|sem_post|sched_lock|spin_lock|spin_trylock|malloc|calloc|ioctl|sleep|usleep)\b', text)
        print('PASS:', board, 'enabled/disabled objects and no-atomic assembly')
        flat_config = config
        for symbol in ['BUILD_PROTECTED', 'BUILD_KERNEL']:
            flat_config = flat_config.replace('CONFIG_'+symbol+'=y', '# CONFIG_'+symbol+' is not set')
        if 'CONFIG_BUILD_FLAT=y' not in flat_config:
            flat_config += '\nCONFIG_BUILD_FLAT=y\n'
        (tmp / '.config').write_text(flat_config + '\nCONFIG_HEALTH_MONITOR=y\n')
        with (tmp / 'tinyara/config.h').open('w') as header:
            subprocess.run([str(tmp / 'mkconfig'), str(tmp)], stdout=header, check=True)
        example_args = [arg for arg in args if arg != '-D__KERNEL__']
        subprocess.run(example_args + ['-c', 'apps/examples/health_monitor/health_monitor_main.c',
                                      '-o', str(tmp / 'example.o')], cwd=ROOT, check=True)
        print('PASS:', board, 'flat example object')

    # Enabling another SMP architecture without a publication port is rejected.
    header = tmp / 'tinyara/config.h'
    header.write_text(header.read_text()+'\n#undef CONFIG_ARCH_CHIP_AMEBASMART\n')
    result = subprocess.run(args + ['-fsyntax-only',
                            'os/kernel/health_monitor/health_monitor.c'],
                            cwd=ROOT, capture_output=True, text=True)
    assert result.returncode and 'Health Monitor SMP requires' in result.stderr
print('PASS: unsupported SMP port rejected')
