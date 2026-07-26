from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


SOURCE = Path(__file__).resolve().parents[3] / "os/dbuild.sh"
COPY_RACE_MARKER = "as it was replaced while being copied"


class DbuildRetryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.os_dir = self.root / "os"
        self.bin_dir = self.root / "fake-bin"
        self.os_dir.mkdir()
        self.bin_dir.mkdir()
        self.script = self.os_dir / "dbuild.sh"
        shutil.copy2(SOURCE, self.script)
        self.script.chmod(0o755)
        (self.os_dir / ".config").write_text(
            'CONFIG_ARCH_BOARD="qemu-armv8m"\n'
            "CONFIG_RAW_BINARY=y\n"
            "CONFIG_BUILD_PARALLEL_JOBS=8\n",
            encoding="utf-8",
        )
        self.count_path = self.root / "docker-run-count"
        fake_docker = self.bin_dir / "docker"
        fake_docker.write_text(
            """#!/usr/bin/env bash
if [ "$1" = "info" ]; then
    if [[ "$*" == *Architecture* ]]; then
        echo aarch64
    else
        echo 8
    fi
    exit 0
fi
if [ "$1" = "image" ] && [ "$2" = "inspect" ]; then
    exit 0
fi
if [ "$1" = "run" ]; then
    count=0
    if [ -f "$FAKE_DOCKER_COUNT" ]; then
        count=$(<"$FAKE_DOCKER_COUNT")
    fi
    count=$((count + 1))
    echo "$count" > "$FAKE_DOCKER_COUNT"
    case "$FAKE_DOCKER_BEHAVIOR" in
        marker-once)
            if [ "$count" -eq 1 ]; then
                echo "install: skipping file 'libuarch.a', as it was replaced while being copied"
                exit 2
            fi
            exit 0
            ;;
        marker-always)
            echo "cp: skipping file 'app1', as it was replaced while being copied"
            exit 9
            ;;
        compiler-error)
            echo "compiler: error: invalid source"
            exit 7
            ;;
        marker-and-compiler-error)
            echo "install: skipping file 'libuarch.a', as it was replaced while being copied"
            echo "compiler: error: invalid source"
            exit 7
            ;;
    esac
fi
exit 1
""",
            encoding="utf-8",
        )
        fake_docker.chmod(0o755)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_dbuild(self, behavior: str, command: str = "build") -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env["PATH"] = f"{self.bin_dir}:{env['PATH']}"
        env["FAKE_DOCKER_COUNT"] = str(self.count_path)
        env["FAKE_DOCKER_BEHAVIOR"] = behavior
        return subprocess.run(
            [str(self.script), command],
            cwd=self.os_dir,
            env=env,
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )

    def run_count(self) -> int:
        return int(self.count_path.read_text(encoding="utf-8"))

    def test_build_retries_copy_race_once(self) -> None:
        completed = self.run_dbuild("marker-once")

        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(2, self.run_count())
        self.assertIn("retrying build once", completed.stdout)
        build_log = (self.os_dir / "build.log").read_text(encoding="utf-8")
        self.assertIn(COPY_RACE_MARKER, build_log)
        self.assertIn("retrying build once", build_log)

    def test_build_propagates_second_copy_race_failure(self) -> None:
        completed = self.run_dbuild("marker-always")

        self.assertEqual(9, completed.returncode)
        self.assertEqual(2, self.run_count())

    def test_build_does_not_retry_unrelated_failure(self) -> None:
        completed = self.run_dbuild("compiler-error")

        self.assertEqual(7, completed.returncode)
        self.assertEqual(1, self.run_count())
        self.assertNotIn("retrying build once", completed.stdout)

    def test_build_retries_once_when_copy_marker_has_other_errors(self) -> None:
        completed = self.run_dbuild("marker-and-compiler-error")

        self.assertEqual(7, completed.returncode)
        self.assertEqual(2, self.run_count())
        self.assertIn("retrying build once", completed.stdout)

    def test_non_build_target_does_not_retry_copy_race(self) -> None:
        completed = self.run_dbuild("marker-always", "clean")

        self.assertEqual(9, completed.returncode)
        self.assertEqual(1, self.run_count())


if __name__ == "__main__":
    unittest.main()
