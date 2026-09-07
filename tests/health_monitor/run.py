#!/usr/bin/env python3
"""Compile the production core against a UP kernel/IRQ/panic test adapter."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    'tinyara/config.h': '#define CONFIG_HEALTH_MONITOR 1\n#define CONFIG_MAX_TASKS 16\n',
    'tinyara/arch.h': '#include <stdbool.h>\nbool up_interrupt_context(void);\nint up_putc(int);\n',
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
    subprocess.run(command, check=True)
    subprocess.run([str(tmp / 'test_core')], check=True)
