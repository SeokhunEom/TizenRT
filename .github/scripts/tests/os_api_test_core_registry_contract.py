from __future__ import annotations

from pathlib import Path
from typing import Final

CoreDescriptor = tuple[str, int, str, str, str, str]

CORE_FAMILY_REGRESSIONS: Final[frozenset[CoreDescriptor]] = frozenset({
    ("TESTIOC_TASK_LIFECYCLE_TEST", 30, "test_task", "task_lifecycle_main", "CONFIG_TC_KERNEL_TASK", "defined(CONFIG_TC_KERNEL_TASK) && defined(CONFIG_SCHED_STARTHOOK) && defined(CONFIG_BUILD_PROTECTED)"),
    ("TESTIOC_SCHED_FOREACH_TEST", 31, "test_sched", "sched_foreach_main", "CONFIG_TC_KERNEL_SCHED", "defined(CONFIG_TC_KERNEL_SCHED)"),
    ("TESTIOC_SIG_FINDACTION_NULL_TEST", 32, "test_signal", "signal_findaction_null_main", "CONFIG_TC_KERNEL_SIGNAL", "defined(CONFIG_TC_KERNEL_SIGNAL) && !defined(CONFIG_DISABLE_SIGNALS)"),
    ("TESTIOC_CLOCK_CONVERSION_TEST", 33, "test_clock", "clock_conversion_main", "CONFIG_TC_KERNEL_CLOCK", "defined(CONFIG_TC_KERNEL_CLOCK)"),
    ("TESTIOC_TIMER_DELETEALL_TEST", 34, "test_timer", "timer_deleteall_main", "CONFIG_TC_KERNEL_TIMER", "defined(CONFIG_TC_KERNEL_TIMER) && !defined(CONFIG_DISABLE_POSIX_TIMERS)"),
    ("TESTIOC_SEM_KERNEL_TEST", 37, "test_sem", "semaphore_kernel_main", "CONFIG_TC_KERNEL_SEMAPHORE", "defined(CONFIG_TC_KERNEL_SEMAPHORE)"),
    ("TESTIOC_PTHREAD_TEST", 40, "test_pthread", "pthread_kernel_main", "CONFIG_TC_KERNEL_PTHREAD", "defined(CONFIG_TC_KERNEL_PTHREAD) && !defined(CONFIG_DISABLE_PTHREAD)"),
    ("TESTIOC_IRQ_TEST", 41, "test_irq", "irq_kernel_main", "CONFIG_TC_KERNEL_IRQ", "defined(CONFIG_TC_KERNEL_IRQ)"),
    ("TESTIOC_SCHED_AFFINITY_TEST", 53, "test_sched", "sched_affinity_main", "CONFIG_TC_KERNEL_SCHED", "defined(CONFIG_TC_KERNEL_SCHED)"),
    ("TESTIOC_GROUP_SIGNAL_TEST", 54, "test_group", "group_signal_main", "CONFIG_TC_KERNEL_GROUP", "defined(CONFIG_TC_KERNEL_GROUP) && defined(CONFIG_SCHED_HAVE_PARENT) && defined(CONFIG_SCHED_CHILD_STATUS) && !defined(CONFIG_DISABLE_SIGNALS)"),
    ("TESTIOC_SCHED_STATE_TEST", 56, "test_sched", "sched_state_main", "CONFIG_TC_KERNEL_SCHED", "defined(CONFIG_TC_KERNEL_SCHED)"),
    ("TESTIOC_SIG_PENDINGSET_TEST", 57, "test_signal", "signal_pendingset_main", "CONFIG_TC_KERNEL_SIGNAL", "defined(CONFIG_TC_KERNEL_SIGNAL) && !defined(CONFIG_DISABLE_SIGNALS)"),
})


def direct_core_dispatch_symbols(root: Path) -> list[str]:
    driver = (root / "os/drivers/os_api_test/os_api_test_drv.c").read_text(encoding="utf-8")
    return sorted(symbol for symbol, *_rest in CORE_FAMILY_REGRESSIONS if symbol in driver)
