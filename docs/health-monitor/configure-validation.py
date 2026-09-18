#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Configure a fresh disposable source copy for stage 7 firmware validation."""

import argparse
import difflib
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="fresh TizenRT source copy")
    parser.add_argument("mode", choices=("off", "on", "test"))
    args = parser.parse_args()
    os_dir = args.source.resolve() / "os"
    config = os_dir / ".config"
    if config.exists() or (os_dir / ".version").exists():
        parser.error("use a fresh source copy: os/.config or os/.version already exists")

    subprocess.run(
        ["./tools/configure.sh", "rtl8730e/loadable_ext_ddr_st7785"],
        cwd=str(os_dir), check=True,
    )
    before = config.read_text()
    fragment = Path(__file__).with_name("rtl8730e-health-monitor.config")
    overrides = dict(line.split("=", 1) for line in fragment.read_text().splitlines()
                     if line.startswith("CONFIG_"))
    if args.mode == "off":
        # Reproduce the pre-activation measurement profile, even though
        # the reference defconfig now enables the monitor and IRQ watchdog.
        overrides.update({
            "CONFIG_HEALTH_MONITOR": "n",
            "CONFIG_WATCHDOG_FOR_IRQ": "n",
            "CONFIG_WATCHDOG_FOR_IRQ_INTERVAL": None,
            "CONFIG_ARCH_HAVE_WDOG_WAKEUP": None,
        })
    overrides["CONFIG_EXAMPLES_HEALTH_MONITOR"] = "y" if args.mode == "test" else "n"
    lines = []
    for line in before.splitlines():
        match = re.fullmatch(r"(CONFIG_\w+)=.*|# (CONFIG_\w+) is not set", line)
        key = (match.group(1) or match.group(2)) if match else None
        if key not in overrides:
            lines.append(line)
    for key, value in overrides.items():
        if value == "n":
            lines.append("# " + key + " is not set")
        elif value is not None:
            lines.append(key + "=" + value)
    after = "\n".join(lines) + "\n"
    config.write_text(after)
    print("".join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                      fromfile="reference", tofile=args.mode)), end="")


if __name__ == "__main__":
    main()
