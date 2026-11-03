#!/usr/bin/env python3
"""Run descriptor adapters against both shipped TF-M header variants."""

import os
from pathlib import Path
import subprocess
import tempfile


def main():
    tests = Path(__file__).resolve().parent
    armino = tests.parent
    os_dir = armino.parents[1]
    firmware = os_dir / "board/bk7239n/src/components/tfm"
    with tempfile.TemporaryDirectory(prefix="armino-key-data-") as scratch:
        temp = Path(scratch)
        (temp / "tinyara").mkdir()
        # Use the actual descriptor definitions without adding all target-only
        # libc headers to the host compiler's include search path.
        (temp / "tinyara/security_hal.h").symlink_to(os_dir / "include/tinyara/security_hal.h")
        for variant in ("tfm_xip_build", "tfm_loadable_build"):
            binary = temp / variant
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra",
                            "-Werror", "-Wstrict-aliasing=2", "-I", str(temp),
                            "-I", str(armino / "include"), "-I",
                            str(firmware / variant / "tfm_install/interface/include"),
                            str(tests / "key_data_test.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("Armino key descriptor tests passed for XIP and loadable TF-M headers")


if __name__ == "__main__":
    main()
