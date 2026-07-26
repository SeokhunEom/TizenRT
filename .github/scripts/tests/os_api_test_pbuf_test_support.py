#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///

# ─── How to run ───
# Imported by test_os_api_test_capabilities.py; it has no standalone CLI.

from __future__ import annotations

import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Final

FIXTURE_ROOT: Final = Path(__file__).parent / "fixtures" / "os_api_test"
PBUF_FIXTURE: Final = FIXTURE_ROOT / "pbuf"
EXPECTED: Final = {
    "pbuf-legacy": "zero=-22 zero_allocs=0 nonzero=1 nonzero_allocs=1",
    "pbuf-kernel": "zero=0 zero_allocs=6 nonzero=-22 nonzero_allocs=0",
    "pbuf-both": "zero=0 zero_allocs=6 nonzero=1 nonzero_allocs=1",
}


@dataclass(frozen=True, slots=True)
class CommandResult:
    returncode: int
    stdout: str
    stderr: str


def config_defines(name: str) -> tuple[str, ...]:
    lines = (FIXTURE_ROOT / f"{name}.config").read_text(encoding="utf-8").splitlines()
    return tuple(f"-D{line.removesuffix('=y')}" for line in lines if line.endswith("=y"))


def _run(command: list[str]) -> CommandResult:
    completed = subprocess.run(command, capture_output=True, text=True, timeout=20, check=False)
    return CommandResult(completed.returncode, completed.stdout.strip(), completed.stderr.strip())


def compile_handler(root: Path, name: str, *, private_abi: bool = False) -> CommandResult:
    with tempfile.TemporaryDirectory(prefix="task11-pbuf-") as directory:
        binary = Path(directory) / "pbuf-matrix"
        source = root / "os" / "drivers" / "os_api_test" / "network" / "test_net_pbuf.c"
        header = root / "os" / "include" / "tinyara" / "os_api_test_drv.h"
        command = [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-DOK=0",
            "-DERROR=-1",
            "-DFAR=",
            f'-DPRODUCT_SOURCE="{source}"',
            f'-DPRODUCT_HEADER="{header}"',
            *config_defines(name),
            f"-I{PBUF_FIXTURE / 'include'}",
            str(PBUF_FIXTURE / "pbuf_harness.c"),
            str(PBUF_FIXTURE / "pbuf_runtime.c"),
            "-o",
            str(binary),
        ]
        if private_abi:
            command.insert(6, "-DPBUF_RED_PRIVATE_ABI")
        compiled = _run(command)
        return compiled if compiled.returncode != 0 else _run([str(binary)])


def compile_header(root: Path, name: str, *, reference_abi: bool = False) -> CommandResult:
    source = PBUF_FIXTURE / ("pbuf_abi_reference.c" if reference_abi else "pbuf_header_probe.c")
    header = root / "os" / "include" / "tinyara" / "os_api_test_drv.h"
    with tempfile.TemporaryDirectory(prefix="task11-header-") as directory:
        binary = Path(directory) / "header-probe"
        return _run([
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            f'-DPRODUCT_HEADER="{header}"',
            *config_defines(name),
            f"-I{PBUF_FIXTURE / 'include'}",
            str(source),
            "-o",
            str(binary),
        ])
