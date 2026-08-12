from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RUNNER = ROOT / ".github" / "scripts" / "qemu-armv8m-ltp.py"
SPEC = importlib.util.spec_from_file_location("qemu_armv8m_ltp", RUNNER)
assert SPEC is not None and SPEC.loader is not None
runner = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = runner
SPEC.loader.exec_module(runner)


class ResultPatternTest(unittest.TestCase):
    def test_requires_a_complete_result_line(self) -> None:
        self.assertIsNone(runner.RESULT_PATTERN.search(b"LTP_RESULT ltp_t1 PA"))
        self.assertIsNone(runner.RESULT_PATTERN.search(b"LTP_RESULT ltp_t1 PASS"))
        match = runner.RESULT_PATTERN.search(b"LTP_RESULT ltp_t1 PASS exit=0\r\n")
        self.assertIsNotNone(match)
        assert match is not None
        self.assertEqual(b"PASS", match.group("status"))
        self.assertEqual(b"0", match.group("exit"))

    def test_accepts_error_result_without_exit_status(self) -> None:
        match = runner.RESULT_PATTERN.search(
            b"LTP_RESULT ltp_t20 ERROR reason=task-create errno=12\n"
        )
        self.assertIsNotNone(match)
        assert match is not None
        self.assertEqual(b"ERROR", match.group("status"))
        self.assertIsNone(match.group("exit"))


class DiscoverTestsTest(unittest.TestCase):
    def make_root(self) -> Path:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        return Path(temporary.name)

    def write_registry(self, root: Path, command: str) -> None:
        registry = root / "apps" / "builtin" / "registry"
        registry.mkdir(parents=True, exist_ok=True)
        (registry / f"{command}.mdat").write_text(
            f'{{ "{command}", ltp_runner_main, 100, 8192 }},\n', encoding="utf-8"
        )

    def write_manifest(self, root: Path, lines: list[str]) -> None:
        manifest = root / "apps" / "examples" / "ltp" / "ltp_manifest.tsv"
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def test_discovers_contiguous_registry_manifest_mapping(self) -> None:
        root = self.make_root()
        self.write_registry(root, "ltp_t1")
        self.write_registry(root, "ltp_t2")
        self.write_manifest(
            root,
            [
                "ltp_t1\tfirst_main\tapps/examples/ltp/ltp/first.c",
                "ltp_t2\tsecond_main\tapps/examples/ltp/ltp/second.c",
            ],
        )

        tests = runner.discover_tests(root)

        self.assertEqual([1, 2], [test.index for test in tests])
        self.assertEqual("second_main", tests[1].function)
        self.assertEqual(("apps/examples/ltp/ltp/second.c",), tests[1].sources)

    def test_rejects_registry_manifest_mismatch(self) -> None:
        root = self.make_root()
        self.write_registry(root, "ltp_t1")
        self.write_manifest(root, ["ltp_t1\tfirst_main\tfirst.c", "ltp_t2\tsecond_main\tsecond.c"])

        with self.assertRaisesRegex(RuntimeError, "registry/manifest mismatch"):
            runner.discover_tests(root)


if __name__ == "__main__":
    unittest.main()
