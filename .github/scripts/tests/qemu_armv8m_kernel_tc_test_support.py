from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType


ResultValue = bool | int | str | None


ROOT = Path(__file__).resolve().parents[3]
SCRIPTS = ROOT / ".github" / "scripts"
RUNNER = SCRIPTS / "qemu-armv8m-kernel-tc.py"


class RunnerLoadError(RuntimeError):
    pass


def load_runner() -> ModuleType:
    if str(SCRIPTS) not in sys.path:
        sys.path.insert(0, str(SCRIPTS))
    spec = importlib.util.spec_from_file_location("qemu_armv8m_kernel_tc", RUNNER)
    if spec is None or spec.loader is None:
        raise RunnerLoadError(f"unable to load {RUNNER}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class RunnerHarness(unittest.TestCase):
    def setUp(self) -> None:
        self.runner = load_runner()
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.log_path = self.root / "artifacts" / "serial.log"
        self.result_path = self.root / "artifacts" / "result.json"

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def request(self):
        return self.runner.RunRequest(
            config="hello",
            root=self.root,
            timeout_sec=1.0,
            log_path=self.log_path,
            result_path=self.result_path,
            verbose=False,
            common_path=None,
            app1_path=None,
            omit_common=False,
            expect_reject=None,
            forbid_marker=None,
            reject_observe_seconds=0.1,
        )

    def child_command(self, script: str, *arguments: str):
        def build(_request):
            return [sys.executable, "-u", "-c", script, *arguments]

        return build

    def read_result(self) -> dict[str, ResultValue]:
        return json.loads(self.result_path.read_text(encoding="utf-8"))

    def write_packages(self) -> tuple[Path, Path, Path]:
        bin_dir = self.root / "build" / "output" / "bin"
        bin_dir.mkdir(parents=True)
        tinyara = bin_dir / "tinyara"
        common = bin_dir / "common"
        app1 = bin_dir / "app1"
        for artifact in (tinyara, common, app1):
            artifact.write_bytes(b"fixture")
        return tinyara, common, app1
