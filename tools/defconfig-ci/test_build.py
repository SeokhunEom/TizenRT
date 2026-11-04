"""Exercise the runner's failure/evidence contract without building firmware."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        for name in ("tools/defconfig-ci", "build/configs/qemu/test",
                     "build/output/bin", "os", "fake-bin"):
            (self.root / name).mkdir(parents=True)
        shutil.copy(Path(__file__).with_name("build.sh"), self.root / "tools/defconfig-ci/build.sh")
        (self.root / "build/configs/qemu/test/defconfig").write_text("CONFIG_TEST=y\n")
        (self.root / "build/output/bin/.gitignore").write_text("*\n!.gitignore\n")
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "-c", "user.name=CI",
                        "-c", "user.email=ci@example.invalid", "commit", "--allow-empty",
                        "-qm", "fixture"], check=True)
        docker = self.root / "fake-bin/docker"
        docker.write_text('''#!/usr/bin/env bash
case "$1" in
  pull) echo "Pull attempted"; exit "${PULL_STATUS:-0}" ;;
  image) echo fixture-image ;;
  run)
    cp "$FIXTURE_ROOT/build/configs/qemu/test/defconfig" "$FIXTURE_ROOT/os/.config"
    echo "Build attempted"
    exit "${BUILD_STATUS:-0}"
    ;;
  rm) exit 0 ;;
  *) exit 99 ;;
esac
''')
        docker.chmod(0o755)
        self.env = {**os.environ, "PATH": f"{self.root / 'fake-bin'}:{os.environ['PATH']}",
                    "FIXTURE_ROOT": str(self.root), "GITHUB_STEP_SUMMARY": str(self.root / "summary")}

    def run_build(self, **env):
        return subprocess.run(["bash", str(self.root / "tools/defconfig-ci/build.sh"),
                               "qemu/test", "tizenrt/tizenrt:1.5.8"],
                              env={**self.env, **env}, text=True, capture_output=True)

    def test_tracked_output_placeholder_is_accepted(self):
        result = self.run_build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "tmp/defconfig-ci/exit-code.txt").read_text(), "0\n")

    def test_existing_firmware_is_rejected(self):
        (self.root / "build/output/bin/tinyara").write_text("stale firmware")
        result = self.run_build()
        self.assertEqual(result.returncode, 2)
        self.assertNotIn("Pull attempted", result.stdout)

    def test_build_failure_survives_tee_and_evidence_collection(self):
        result = self.run_build(BUILD_STATUS="42")
        self.assertEqual(result.returncode, 42, result.stdout + result.stderr)
        output = self.root / "tmp/defconfig-ci"
        self.assertEqual((output / "exit-code.txt").read_text(), "42\n")
        self.assertIn("Build attempted", (output / "build.log").read_text())
        self.assertTrue((output / "effective.config").is_file())

    def test_image_pull_failure_is_not_reported_as_a_build_pass(self):
        result = self.run_build(PULL_STATUS="17")
        self.assertEqual(result.returncode, 17, result.stdout + result.stderr)
        output = self.root / "tmp/defconfig-ci"
        self.assertEqual((output / "exit-code.txt").read_text(), "17\n")
        self.assertFalse((output / "build.log").exists())


if __name__ == "__main__":
    unittest.main()
