#!/usr/bin/env python3
"""Compile the production core against deterministic UP/SMP word-access adapters."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    'tinyara/config.h': '#define CONFIG_HEALTH_MONITOR 1\n#define CONFIG_BUILD_FLAT 1\n#define CONFIG_MAX_TASKS 16\n#ifdef TEST_SMP\n#define CONFIG_SMP 1\n#define CONFIG_SMP_NCPUS 2\n#define CONFIG_ARCH_CHIP_AMEBASMART 1\n#endif\n',
    'tinyara/arch.h': '#include <stdbool.h>\nbool up_interrupt_context(void);\nint up_putc(int);\nint up_cpu_index(void);\n',
    'tinyara/irq.h': 'typedef unsigned int irqstate_t;\nirqstate_t irqsave(void);\nvoid irqrestore(irqstate_t);\n',
    'tinyara/clock.h': '#define USEC_PER_TICK 10000\n',
    'tinyara/sched.h': '''#include <sys/types.h>
#define PIDHASH(pid) ((pid) & (CONFIG_MAX_TASKS - 1))
struct tcb_s { pid_t pid; unsigned int task_state, sched_priority, lockcount;
void *waitsem, *stack_alloc_ptr; };
struct tcb_s *sched_self(void);
void sched_foreach(void (*handler)(struct tcb_s *, void *), void *arg);
''',
}
def run():
    with tempfile.TemporaryDirectory(prefix='health-monitor-') as directory:
        tmp = Path(directory)
        for name, contents in HEADERS.items():
            path = tmp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
        command = [os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                   '-g', '-fsanitize=address,undefined', '-I'+str(tmp),
                   '-I'+str(ROOT / 'os/include'), str(Path(__file__).with_name('test_core.c')),
                   '-o', str(tmp / 'test_core')]
        # Do not add all OS headers: host libc must remain authoritative.
        (tmp / 'tinyara/health_monitor.h').write_text(
            (ROOT / 'os/include/tinyara/health_monitor.h').read_text())
        command.remove('-I'+str(ROOT / 'os/include'))
        for smp in (False, True):
            subprocess.run(command + (['-DTEST_SMP=1'] if smp else []), check=True)
            subprocess.run([str(tmp / 'test_core')], check=True)

        thread_command = command.copy()
        thread_command[thread_command.index(str(Path(__file__).with_name('test_core.c')))] = str(Path(__file__).with_name('test_threads.c'))
        if os.environ.get('HEALTH_TSAN') == '1':
            thread_command[thread_command.index('-fsanitize=address,undefined')] = '-fsanitize=thread'
        subprocess.run(thread_command + ['-DTEST_SMP=1', '-pthread'], check=True)
        subprocess.run([str(tmp / 'test_core')], check=True)


if __name__ == "__main__":
    run()
