from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Final

from os_api_test_registry_syntax import C_DIMENSIONS, REGISTRY


CC_PREPROCESS: Final = ("cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-DCONFIG_TC_KERNEL_SYNTHETIC", "-E", "-P")


def preprocess(path: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run([*CC_PREPROCESS, str(path)], capture_output=True, text=True, timeout=10, check=False)


def fixture(root: Path, dimensions: set[str]) -> None:
    files = {
        REGISTRY: "#if defined(CONFIG_TC_KERNEL_SYNTHETIC)\nOS_API_TEST_KERNEL_DESCRIPTOR(TESTIOC_SYNTHETIC, 200, test_synthetic, test_synthetic.c, synthetic_main, tc_synthetic.c, CONFIG_TC_KERNEL_SYNTHETIC)\n#endif\n",
        C_DIMENSIONS["header"][0]: "#define OS_API_TEST_KERNEL_DESCRIPTOR(symbol, id, provider, provider_source, wrapper, wrapper_source, test_gate) enum { symbol = _TESTIOC(id) };\n#include \"../../drivers/os_api_test/os_api_test_kernel_registry.inc\"\n#undef OS_API_TEST_KERNEL_DESCRIPTOR\n" if "header" in dimensions else "",
        C_DIMENSIONS["prototype"][0]: "#define OS_API_TEST_KERNEL_DESCRIPTOR(symbol, id, provider, provider_source, wrapper, wrapper_source, test_gate) int provider(int cmd, unsigned long arg);\n#include \"os_api_test_kernel_registry.inc\"\n#undef OS_API_TEST_KERNEL_DESCRIPTOR\n" if "prototype" in dimensions else "",
        C_DIMENSIONS["dispatch"][0]: "#define OS_API_TEST_KERNEL_DESCRIPTOR(symbol, id, provider, provider_source, wrapper, wrapper_source, test_gate) case symbol: ret = provider(cmd, arg); break;\n#include \"os_api_test_kernel_registry.inc\"\n#undef OS_API_TEST_KERNEL_DESCRIPTOR\n" if "dispatch" in dimensions else "",
        C_DIMENSIONS["wrapper"][0]: "#define OS_API_TEST_KERNEL_DESCRIPTOR(symbol, id, provider, provider_source, wrapper, wrapper_source, test_gate) wrapper();\n#include \"../../../../../os/drivers/os_api_test/os_api_test_kernel_registry.inc\"\n#undef OS_API_TEST_KERNEL_DESCRIPTOR\n" if "wrapper" in dimensions else "",
        Path("os/drivers/os_api_test/kernel/Make.defs"): "ifeq ($(CONFIG_TC_KERNEL_SYNTHETIC),y)\nCSRCS += test_synthetic.c\nendif\n" if "provider-source" in dimensions else "",
        Path("apps/examples/testcase/le_tc/kernel/Make.defs"): "ifeq ($(CONFIG_TC_KERNEL_SYNTHETIC),y)\nCSRCS += tc_synthetic.c\nendif\n" if "wrapper-source" in dimensions else "",
        Path("apps/examples/testcase/le_tc/kernel/Kconfig"): "config TC_KERNEL_SYNTHETIC\n\tbool \"synthetic\"\n" if "kconfig" in dimensions else "",
    }
    for path, content in files.items():
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")
