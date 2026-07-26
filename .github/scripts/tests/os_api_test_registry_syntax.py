from __future__ import annotations

import re
from pathlib import Path
from typing import Final


REGISTRY: Final = Path("os/drivers/os_api_test/os_api_test_kernel_registry.inc")
C_DIMENSIONS: Final = {
    "header": (Path("os/include/tinyara/os_api_test_drv.h"), "enum { symbol = _TESTIOC(id) };"),
    "prototype": (Path("os/drivers/os_api_test/os_api_test_proto.h"), "int provider(int cmd, unsigned long arg);"),
    "dispatch": (Path("os/drivers/os_api_test/os_api_test_drv.c"), "case symbol:"),
    "wrapper": (Path("apps/examples/testcase/le_tc/kernel/kernel_tc_main.c"), "wrapper();"),
}


def make_has_source(text: str, gate: str, source: str) -> bool:
    text = "\n".join(line.partition("#")[0] for line in text.splitlines())
    pattern = rf"ifeq\s*\(\$\({re.escape(gate)}\),y\)(?:(?!^endif).)*?CSRCS\s*\+=\s*{re.escape(source)}\s*$"
    return re.search(pattern, text, re.MULTILINE | re.DOTALL) is not None


def has_c_adapter(text: str, signature: str) -> bool:
    text = re.sub(r"\\(?:\r\n|\n|\r)", "", text)
    text = re.sub(r"/\*.*?\*/|//[^\n]*", lambda comment: " " + "\n" * comment.group().count("\n"), text, flags=re.DOTALL)
    include = re.search(r'(?m)^\s*#\s*include\s+"[^"\n]*os_api_test_kernel_registry\.inc"\s*$', text)
    return include is not None and signature in text
