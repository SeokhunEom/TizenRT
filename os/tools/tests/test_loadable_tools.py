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

import os
import shutil
import stat
import struct
import subprocess
import unittest
import zlib
from pathlib import Path

from loadable_tools_test_case import LoadableToolTestCase
from loadable_tools_test_support import checksum_is_valid


class LoadableToolTests(LoadableToolTestCase):
    def test_checksum_prepends_crc_and_preserves_ascii_payload(self) -> None:
        # Given: a text-compatible payload in an isolated output file.
        payload = b"loadable-package\n"
        artifact = self.root / "app.bin"
        artifact.write_bytes(payload)

        # When: the checksum tool packages the payload.
        result = self.sandbox.prepend_checksum(artifact)

        # Then: the observable package is CRC followed by the original bytes.
        expected_crc = struct.pack("I", zlib.crc32(payload) & 0xFFFFFFFF)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(artifact.read_bytes(), expected_crc + payload)

    def test_checksum_preserves_non_text_bytes(self) -> None:
        # Given: a payload that cannot be decoded as UTF-8 text.
        payload = b"\x00\xff\x80binary\r\n"
        artifact = self.root / "binary.bin"
        artifact.write_bytes(payload)

        # When: the checksum tool reads and rewrites the artifact.
        result = self.sandbox.prepend_checksum(artifact)

        # Then: the CRC and all payload bytes are preserved exactly.
        expected_crc = struct.pack("I", zlib.crc32(payload) & 0xFFFFFFFF)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(artifact.read_bytes(), expected_crc + payload)

    def test_binary_header_and_checksum_have_exact_layout(self) -> None:
        # Given: a small payload containing bytes that are invalid UTF-8.
        payload = b"\x00\xff\x80\r\nfixture\x00"
        working_directory = self.root / "package"
        working_directory.mkdir()

        # When: the user-header and checksum tools package it.
        artifact = self.sandbox.build_user_package(working_directory, payload)
        header_and_payload = artifact.read_bytes()
        result = self.sandbox.prepend_checksum(artifact)

        # Then: every field has its documented byte length and position.
        expected_header = b"".join(
            (
                struct.pack("H", 41),
                struct.pack("B", 1),
                struct.pack("B", 100),
                struct.pack("B", 1),
                struct.pack("I", len(payload)),
                b"fixture" + (b"\x00" * 9),
                struct.pack("I", 260718),
                struct.pack("I", 8192),
                struct.pack("I", 1024),
                struct.pack("I", 260718),
            )
        )
        expected_crc = struct.pack("I", zlib.crc32(expected_header + payload) & 0xFFFFFFFF)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(expected_header), 41)
        self.assertEqual(header_and_payload, expected_header + payload)
        self.assertEqual(artifact.read_bytes(), expected_crc + expected_header + payload)

    def test_binary_package_is_deterministic_across_clean_outputs(self) -> None:
        # Given: two clean work directories and one binary payload.
        payload = bytes(range(32)) + b"\xff"
        first_directory = self.root / "first"
        second_directory = self.root / "second"
        first_directory.mkdir()
        second_directory.mkdir()

        # When: both outputs are independently rebuilt from the payload.
        first = self.sandbox.build_user_package(first_directory, payload)
        second = self.sandbox.build_user_package(second_directory, payload)
        first_result = self.sandbox.prepend_checksum(first)
        second_result = self.sandbox.prepend_checksum(second)

        # Then: stale state cannot affect their byte-for-byte identity.
        self.assertEqual(first_result.returncode, 0, first_result.stderr.decode())
        self.assertEqual(second_result.returncode, 0, second_result.stderr.decode())
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_user_header_rejects_empty_binary_name_atomically(self) -> None:
        # Given: an existing payload and an empty user-binary name.
        binary_name = ""

        # When: the header tool parses the malformed name.
        result, artifact = self._invoke_invalid_binary_name(binary_name)

        # Then: it fails before changing the payload or creating a backup.
        self._assert_invalid_name_failure(result, artifact)

    def test_user_header_rejects_overlong_binary_name_atomically(self) -> None:
        # Given: an existing payload and a name that leaves no NUL terminator.
        binary_name = "A" * 16

        # When: the header tool parses the overlong name.
        result, artifact = self._invoke_invalid_binary_name(binary_name)

        # Then: it fails before changing the payload or creating a backup.
        self._assert_invalid_name_failure(result, artifact)

    def test_user_header_rejects_non_ascii_binary_name_atomically(self) -> None:
        # Given: an existing payload and a non-ASCII user-binary name.
        binary_name = "fixtur\N{LATIN SMALL LETTER E WITH ACUTE}"

        # When: the header tool parses the non-ASCII name.
        result, artifact = self._invoke_invalid_binary_name(binary_name)

        # Then: it fails before changing the payload or creating a backup.
        self._assert_invalid_name_failure(result, artifact)

    def test_user_header_accepts_maximum_ascii_binary_name(self) -> None:
        # Given: the maximum 15-byte ASCII name for a 16-byte field.
        binary_name = "A" * 15
        working_directory = self.root / "maximum-name"
        working_directory.mkdir()

        # When: the header tool packages the payload.
        result, artifact = self.sandbox.invoke_user_header(
            working_directory,
            b"payload",
            binary_name,
        )

        # Then: the field contains the name and exactly one NUL terminator.
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(artifact.read_bytes()[9:25], binary_name.encode("ascii") + b"\x00")

    def test_resource_header_refactor_preserves_exact_bytes(self) -> None:
        # Given: a small binary resource payload.
        payload = b"\x00\xffresource"
        working_directory = self.root / "resource-header"
        working_directory.mkdir()

        # When: the resource header is generated through the CLI.
        result, artifact = self.sandbox.invoke_resource_header(working_directory, payload)

        # Then: its original field sizes, padding, and payload placement remain exact.
        expected_header = b"".join(
            (
                struct.pack("H", 10),
                struct.pack("I", 260718),
                struct.pack("I", len(payload)),
                b"\xff" * 4082,
            )
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(expected_header), 4092)
        self.assertEqual(artifact.read_bytes(), expected_header + payload)

    def test_checksum_handles_empty_payload_and_verifier_rejects_truncation(self) -> None:
        # Given: an empty payload and packages shorter than the CRC field.
        artifact = self.root / "empty.bin"
        artifact.write_bytes(b"")

        # When: the checksum tool packages the empty payload.
        result = self.sandbox.prepend_checksum(artifact)

        # Then: the CRC is deterministic and truncated packages are invalid.
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(artifact.read_bytes(), b"\x00\x00\x00\x00")
        self.assertTrue(checksum_is_valid(artifact.read_bytes()))
        self.assertFalse(checksum_is_valid(b""))
        self.assertFalse(checksum_is_valid(b"\x00\x00\x00"))

    def test_checksum_verification_fails_after_payload_mutation(self) -> None:
        # Given: a checksummed binary package.
        artifact = self.root / "mutated.bin"
        artifact.write_bytes(b"immutable-payload")
        result = self.sandbox.prepend_checksum(artifact)
        package = bytearray(artifact.read_bytes())

        # When: one payload byte changes after checksum generation.
        package[-1] ^= 0x01

        # Then: byte-level verification rejects the package.
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertFalse(checksum_is_valid(bytes(package)))

    def test_verify_app_uses_configured_target_nm(self) -> None:
        # Given: target and host nm shims with independently observable logs.
        make = shutil.which("make")
        if make is None:
            self.skipTest("make is required")
        working_directory = self.root / "make"
        working_directory.mkdir()
        artifact_directory = working_directory / "bin"
        artifact_directory.mkdir()
        (artifact_directory / "app.elf").write_bytes(b"elf")
        target_log = working_directory / "target.log"
        host_log = working_directory / "host.log"
        target_nm = self._write_nm_shim(working_directory / "target-nm", target_log)
        host_bin = working_directory / "host-bin"
        host_bin.mkdir()
        self._write_nm_shim(host_bin / "nm", host_log)
        makefile = self._write_verify_makefile(working_directory, target_nm)

        # When: the real VERIFY_APP definition checks the artifact.
        environment = {"PATH": f"{host_bin}{os.pathsep}{os.environ['PATH']}"}
        result = self.sandbox.run([make, "-f", str(makefile), "verify"], working_directory, environment)

        # Then: only the configured target nm is invoked.
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertTrue(target_log.exists())
        self.assertIn("-u", target_log.read_text(encoding="utf-8"))
        self.assertFalse(host_log.exists())

    def test_conditional_non_pointer_syscall_stub_compiles(self) -> None:
        # Given: a conditional syscall returning a non-pointer size type.
        working_directory = self.root / "syscall"
        working_directory.mkdir()
        csv_line = '"fixture_status","fixture_api.h","defined(CONFIG_FIXTURE_STATUS)","size_t","int"\n'

        # When: mksyscall generates a disabled-feature stub and the host compiles it.
        generate_result, source = self.sandbox.generate_stub(working_directory, csv_line)
        compile_result = self.sandbox.compile_stub(working_directory, source)

        # Then: the fallback is an integer guard and generated C is valid.
        generated = source.read_text(encoding="utf-8")
        self.assertEqual(generate_result.returncode, 0, generate_result.stderr.decode())
        self.assertEqual(compile_result.returncode, 0, compile_result.stderr.decode())
        self.assertIn("#if defined(CONFIG_FIXTURE_STATUS)", generated)
        self.assertIn("return 0;", generated)
        self.assertNotIn("return NULL;", generated)

    def test_malformed_syscall_csv_is_rejected(self) -> None:
        # Given: a syscall record missing its condition and return signature.
        working_directory = self.root / "malformed-syscall"
        working_directory.mkdir()

        # When: mksyscall parses the incomplete record.
        result, source = self.sandbox.generate_stub(working_directory, '"fixture_status","fixture_api.h"\n')

        # Then: generation fails and no stale output is accepted.
        self.assertEqual(result.returncode, 8)
        self.assertFalse(source.exists())

    def test_invalid_syscall_conditional_fails_generated_source_compile(self) -> None:
        # Given: a complete record with a syntactically invalid conditional.
        working_directory = self.root / "invalid-condition"
        working_directory.mkdir()
        csv_line = '"fixture_status","fixture_api.h","defined(","size_t","int"\n'

        # When: the generated source is compiled through the host seam.
        generate_result, source = self.sandbox.generate_stub(working_directory, csv_line)
        compile_result = self.sandbox.compile_stub(working_directory, source)

        # Then: generation is observable but invalid C cannot report success.
        self.assertEqual(generate_result.returncode, 0, generate_result.stderr.decode())
        self.assertNotEqual(compile_result.returncode, 0)

    def test_dbuild_normalizes_uppercase_and_lowercase_options_on_bash3(self) -> None:
        # Given: SELECT_OPTION in a Bash 3 harness with build actions stubbed.
        bash = Path("/bin/bash")
        if not bash.exists():
            self.skipTest("/bin/bash is required")
        harness = self._write_dbuild_harness()

        # When: equivalent lowercase and uppercase tokens are selected.
        results = {
            token: self.sandbox.run([str(bash), str(harness), token], self.root)
            for token in ("build", "BUILD", "clean", "CLEAN")
        }

        # Then: case does not change the selected build action.
        self.assertEqual(results["build"].stdout, b"BUILD:\n")
        self.assertEqual(results["BUILD"].stdout, results["build"].stdout)
        self.assertEqual(results["clean"].stdout, b"BUILD:clean\n")
        self.assertEqual(results["CLEAN"].stdout, results["clean"].stdout)
        self.assertTrue(all(result.returncode == 0 for result in results.values()))

    def test_dbuild_unsupported_option_performs_no_action(self) -> None:
        # Given: SELECT_OPTION in the isolated Bash behavior harness.
        harness = self._write_dbuild_harness()

        # When: an unsupported option reaches the dispatch boundary.
        result = self.sandbox.run(["/bin/bash", str(harness), "unsupported"], self.root)

        # Then: no build action is accidentally selected.
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(result.stdout, b"")

    def _write_nm_shim(self, path: Path, log: Path) -> Path:
        path.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$*\" >> '{log}'\n", encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def _invoke_invalid_binary_name(
        self,
        binary_name: str,
    ) -> tuple[subprocess.CompletedProcess[bytes], Path]:
        working_directory = self.root / f"invalid-name-{len(list(self.root.iterdir()))}"
        working_directory.mkdir()
        (working_directory / "fixture_without_header.bin").write_bytes(b"preexisting-backup")
        return self.sandbox.invoke_user_header(
            working_directory,
            b"original-payload",
            binary_name,
        )

    def _assert_invalid_name_failure(
        self,
        result: subprocess.CompletedProcess[bytes],
        artifact: Path,
    ) -> None:
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(artifact.read_bytes(), b"original-payload")
        backup = artifact.with_name(f"{artifact.stem}_without_header{artifact.suffix}")
        self.assertEqual(backup.read_bytes(), b"preexisting-backup")

    def _write_verify_makefile(self, working_directory: Path, target_nm: Path) -> Path:
        source = (self.sandbox.os_dir / "Makefile.unix").read_text(encoding="utf-8")
        start = source.index("define VERIFY_APP")
        end = source.index("endef", start) + len("endef")
        makefile = working_directory / "Makefile"
        makefile.write_text(
            f"Q :=\nOUTBIN_DIR := {working_directory / 'bin'}\nNM := {target_nm}\n"
            + source[start:end]
            + "\n.PHONY: verify\nverify:\n\t$(call VERIFY_APP,app.elf,0)\n",
            encoding="utf-8",
        )
        return makefile

    def _write_dbuild_harness(self) -> Path:
        source = (self.sandbox.os_dir / "dbuild.sh").read_text(encoding="utf-8")
        start = source.index("function SELECT_OPTION()")
        end = source.index("\nfunction BUILD_TEST()", start)
        os_dir = self.root / "dbuild-os"
        os_dir.mkdir(exist_ok=True)
        (os_dir / ".config").write_text("CONFIG_FIXTURE=y\n", encoding="utf-8")
        harness = self.root / "dbuild-harness.sh"
        harness.write_text(
            f"OSDIR='{os_dir}'\nSTATUS=CONFIGURED\n"
            "BUILD() { printf 'BUILD:%s\\n' \"$*\"; }\n"
            "BUILD_TEST() { printf 'BUILD_TEST\\n'; }\n"
            "SELECT_BOARD() { printf 'SELECT_BOARD\\n'; }\n"
            + source[start:end]
            + "\nSELECT_OPTION \"$1\"\n",
            encoding="utf-8",
        )
        return harness


if __name__ == "__main__":
    unittest.main()
