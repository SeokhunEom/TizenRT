#!/usr/bin/env python3
############################################################################
#
# Copyright 2026 Samsung Electronics All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
############################################################################

from __future__ import annotations

import os
import shutil
import stat
import struct
import subprocess
import sys
import unittest
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Final, Sequence


COMMAND_TIMEOUT_SECONDS: Final = 10
TOOL_FILENAMES: Final = (
    "config_util.py",
    "csvparser.c",
    "csvparser.h",
    "loadable_binary_header.py",
    "mkbinheader.py",
    "mkchecksum.py",
    "mksyscall.c",
)


@dataclass(frozen=True, slots=True)
class ToolSandbox:
    root: Path
    os_dir: Path
    tools_dir: Path

    @classmethod
    def copy_from(cls, source_tools: Path, destination: Path) -> ToolSandbox:
        os_dir = destination / "os"
        tools_dir = os_dir / "tools"
        tools_dir.mkdir(parents=True)
        for filename in TOOL_FILENAMES:
            shutil.copy2(source_tools / filename, tools_dir / filename)
        shutil.copy2(source_tools.parent / "Makefile.unix", os_dir / "Makefile.unix")
        shutil.copy2(source_tools.parent / "dbuild.sh", os_dir / "dbuild.sh")
        return cls(root=destination, os_dir=os_dir, tools_dir=tools_dir)

    def run(
        self,
        command: Sequence[str],
        working_directory: Path,
        environment: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[bytes]:
        process_environment = os.environ.copy()
        process_environment["PYTHONDONTWRITEBYTECODE"] = "1"
        if environment is not None:
            process_environment.update(environment)
        return subprocess.run(
            command,
            cwd=working_directory,
            env=process_environment,
            check=False,
            capture_output=True,
            timeout=COMMAND_TIMEOUT_SECONDS,
        )

    def build_user_package(self, working_directory: Path, payload: bytes) -> Path:
        result, artifact = self.invoke_user_header(working_directory, payload, "fixture")
        if result.returncode != 0:
            raise AssertionError(result.stderr.decode() + result.stdout.decode())
        return artifact

    def invoke_user_header(
        self,
        working_directory: Path,
        payload: bytes,
        binary_name: str,
    ) -> tuple[subprocess.CompletedProcess[bytes], Path]:
        artifact = working_directory / "fixture.bin"
        artifact.write_bytes(payload)
        (working_directory / "fixture.bin_dbg").write_bytes(payload)
        self._write_user_config()
        tool_bin = working_directory / "tool-bin"
        tool_bin.mkdir()
        readelf = tool_bin / "readelf"
        readelf.write_text(
            "#!/bin/sh\n"
            "printf '%s\\n' "
            "'[ 1] .text PROGBITS 00000000 000040 000003' "
            "'[ 2] .rodata PROGBITS 00000000 000050 000002' "
            "'[ 3] .data PROGBITS 00000000 000060 000001' "
            "'[ 4] .bss NOBITS 00000000 000070 000004'\n",
            encoding="utf-8",
        )
        readelf.chmod(readelf.stat().st_mode | stat.S_IXUSR)
        environment = {"PATH": f"{tool_bin}{os.pathsep}{os.environ['PATH']}"}
        result = self.run(
            [
                sys.executable,
                str(self.tools_dir / "mkbinheader.py"),
                str(artifact),
                "user",
                "elf",
                binary_name,
                "260718",
                "4096",
                "1024",
                "100",
                "LOW",
            ],
            working_directory,
            environment,
        )
        return result, artifact

    def prepend_checksum(self, artifact: Path) -> subprocess.CompletedProcess[bytes]:
        return self.run(
            [sys.executable, str(self.tools_dir / "mkchecksum.py"), str(artifact)],
            artifact.parent,
        )

    def invoke_resource_header(
        self,
        working_directory: Path,
        payload: bytes,
    ) -> tuple[subprocess.CompletedProcess[bytes], Path]:
        artifact = working_directory / "resource.bin"
        artifact.write_bytes(payload)
        self._write_user_config()
        result = self.run(
            [
                sys.executable,
                str(self.tools_dir / "mkbinheader.py"),
                str(artifact),
                "resource",
            ],
            working_directory,
        )
        return result, artifact

    def compile_mksyscall(self, working_directory: Path) -> Path:
        compiler = shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("a host C compiler is required")
        executable = working_directory / "mksyscall"
        result = self.run(
            [
                compiler,
                "-std=gnu99",
                "-Wall",
                "-Wextra",
                "-o",
                str(executable),
                str(self.tools_dir / "mksyscall.c"),
                str(self.tools_dir / "csvparser.c"),
            ],
            working_directory,
        )
        if result.returncode != 0:
            raise AssertionError(result.stderr.decode())
        return executable

    def generate_stub(
        self,
        working_directory: Path,
        csv_line: str,
    ) -> tuple[subprocess.CompletedProcess[bytes], Path]:
        executable = self.compile_mksyscall(working_directory)
        csv_path = working_directory / "fixture.csv"
        csv_path.write_text(csv_line, encoding="utf-8")
        result = self.run([str(executable), "-s", str(csv_path)], working_directory)
        return result, working_directory / "STUB_fixture_status.c"

    def compile_stub(
        self,
        working_directory: Path,
        source: Path,
    ) -> subprocess.CompletedProcess[bytes]:
        compiler = shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("a host C compiler is required")
        include = working_directory / "include"
        (include / "tinyara").mkdir(parents=True)
        (include / "tinyara" / "config.h").write_text(
            "#define set_errno(value) ((void)(value))\n",
            encoding="utf-8",
        )
        (include / "fixture_api.h").write_text(
            "#include <stddef.h>\nsize_t fixture_status(int value);\n",
            encoding="utf-8",
        )
        return self.run(
            [
                compiler,
                "-std=c99",
                "-Werror",
                "-Werror=int-conversion",
                "-I",
                str(include),
                "-c",
                str(source),
                "-o",
                str(working_directory / "fixture.o"),
            ],
            working_directory,
        )

    def _write_user_config(self) -> None:
        (self.os_dir / ".config").write_text(
            'CONFIG_BOARD_BUILD_DATE="260718"\n'
            "CONFIG_BM_PRIORITY_MAX=205\n"
            "CONFIG_BM_PRIORITY_MIN=200\n"
            'CONFIG_RESOURCE_BINARY_VERSION="260718"\n',
            encoding="utf-8",
        )


def checksum_is_valid(package: bytes) -> bool:
    if len(package) < struct.calcsize("I"):
        return False
    checksum_size = struct.calcsize("I")
    expected = struct.pack("I", zlib.crc32(package[checksum_size:]) & 0xFFFFFFFF)
    return package[:checksum_size] == expected
