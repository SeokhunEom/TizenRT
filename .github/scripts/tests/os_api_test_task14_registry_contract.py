from __future__ import annotations

from pathlib import Path
from typing import Final

Task14Descriptor = tuple[str, int, str, str, str, str]

TASK14_REGRESSIONS: Final[frozenset[Task14Descriptor]] = frozenset({
    ("TESTIOC_KMM_HEAP_TEST", 29, "test_kmm", "umm_heap_main", "CONFIG_TC_KERNEL_UMM_HEAP", "defined(CONFIG_TC_KERNEL_UMM_HEAP) && defined(CONFIG_MM_KERNEL_HEAP)"),
    ("TESTIOC_MQUEUE_TEST", 36, "test_mqueue", "mqueue_main", "CONFIG_TC_KERNEL_MQUEUE", "defined(CONFIG_TC_KERNEL_MQUEUE) && !defined(CONFIG_DISABLE_MQUEUE)"),
    ("TESTIOC_ENVIRON_TEST", 38, "test_environ", "environ_main", "CONFIG_TC_KERNEL_ENVIRON", "defined(CONFIG_TC_KERNEL_ENVIRON) && !defined(CONFIG_DISABLE_ENVIRON)"),
    ("TESTIOC_ERRNO_TEST", 39, "test_errno", "errno_main", "CONFIG_TC_KERNEL_ERRNO", "defined(CONFIG_TC_KERNEL_ERRNO)"),
    ("TESTIOC_PROCFS_TEST", 47, "test_procfs", "procfs_main", "CONFIG_TC_KERNEL_PROCFS", "defined(CONFIG_TC_KERNEL_PROCFS) && defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_UPTIME) && !defined(CONFIG_FS_PROCFS_EXCLUDE_VERSION)"),
    ("TESTIOC_PIPE_TEST", 49, "test_pipe", "pipe_main", "CONFIG_TC_KERNEL_PIPE", "defined(CONFIG_TC_KERNEL_PIPE) && defined(CONFIG_PIPES)"),
    ("TESTIOC_VFS_TEST", 51, "test_vfs", "vfs_main", "CONFIG_TC_KERNEL_VFS", "defined(CONFIG_TC_KERNEL_VFS)"),
    ("TESTIOC_TERMIOS_TEST", 52, "test_termios", "termios_main", "CONFIG_TC_KERNEL_TERMIOS", "defined(CONFIG_TC_KERNEL_TERMIOS) && defined(CONFIG_SERIAL_TERMIOS)"),
})


def direct_task14_dispatch_symbols(root: Path) -> list[str]:
    driver = (root / "os/drivers/os_api_test/os_api_test_drv.c").read_text(encoding="utf-8")
    return sorted(symbol for symbol, *_rest in TASK14_REGRESSIONS if symbol in driver)
