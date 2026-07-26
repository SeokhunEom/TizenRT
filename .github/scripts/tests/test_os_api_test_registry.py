#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv or pip install needed):
#      uv run test_os_api_test_registry.py --root /path/to/TizenRT
# 3. Or run the repository suite:
#      python3 -m unittest discover -s .github/scripts/tests -p 'test_os_api_test_registry.py' -v
# ─────────────────

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
import unittest
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Final, TypedDict

from os_api_test_core_registry_contract import CORE_FAMILY_REGRESSIONS, direct_core_dispatch_symbols
from os_api_test_registry_syntax import C_DIMENSIONS, REGISTRY, has_c_adapter, make_has_source
from os_api_test_registry_test_support import fixture, preprocess

ROW_RE: Final = re.compile(
    r"OS_API_TEST_KERNEL_DESCRIPTOR\("
    r"(TESTIOC_[A-Z0-9_]+), ([0-9]+), ([a-z][a-z0-9_]*), "
    r"([a-z][a-z0-9_]*\.c), ([a-z][a-z0-9_]*), "
    r"([a-z][a-z0-9_]*\.c), (CONFIG_TC_KERNEL_[A-Z0-9_]+)\)"
)
ATOM: Final = r"!?defined\(CONFIG_[A-Z][A-Z0-9_]*\)"
PREDICATE_RE: Final = re.compile(rf"{ATOM}(?: && {ATOM})*")
@dataclass(frozen=True, slots=True)
class Descriptor:
    symbol: str
    command_id: int
    provider: str
    provider_source: str
    wrapper: str
    wrapper_source: str
    test_gate: str
    predicate: str


class Issue(TypedDict):
    symbol: str
    dimension: str
    message: str


class Report(TypedDict):
    ok: bool
    descriptor_count: int
    descriptors: list[dict[str, str | int]]
    errors: list[Issue]


def _issue(symbol: str, dimension: str, message: str) -> Issue:
    return {"symbol": symbol, "dimension": dimension, "message": message}


def _parse_registry(path: Path) -> tuple[list[Descriptor], list[Issue]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError:
        return [], [_issue("registry", "registry", f"missing {path.as_posix()}")]
    descriptors: list[Descriptor] = []
    errors: list[Issue] = []
    predicate: str | None = None
    for line_number, line in enumerate(lines, 1):
        stripped = line.strip()
        if not stripped or stripped.startswith(("/*", "*", "*/", "//")):
            continue
        if stripped.startswith("#if "):
            candidate = stripped.removeprefix("#if ")
            if predicate is not None or PREDICATE_RE.fullmatch(candidate) is None:
                errors.append(_issue("registry", "predicate", f"line {line_number}: malformed predicate {candidate!r}"))
            else:
                predicate = candidate
            continue
        if stripped == "#endif":
            if predicate is None:
                errors.append(_issue("registry", "predicate", f"line {line_number}: unmatched #endif"))
            predicate = None
            continue
        match = ROW_RE.fullmatch(stripped)
        if match is None:
            errors.append(_issue("registry", "row", f"line {line_number}: malformed registry row"))
            continue
        symbol, command_id, provider, provider_source, wrapper, wrapper_source, test_gate = match.groups()
        if predicate is None:
            errors.append(_issue(symbol, "predicate", f"{symbol}: missing predicate"))
            continue
        if f"defined({test_gate})" not in predicate.split(" && "):
            errors.append(_issue(symbol, "predicate", f"{symbol}: predicate must enable {test_gate}"))
        descriptors.append(Descriptor(symbol, int(command_id), provider, provider_source, wrapper, wrapper_source, test_gate, predicate))
    if predicate is not None:
        errors.append(_issue("registry", "predicate", "unterminated predicate"))
    return descriptors, errors


def _read(root: Path, relative: Path, errors: list[Issue]) -> str:
    try:
        return (root / relative).read_text(encoding="utf-8")
    except FileNotFoundError:
        errors.append(_issue("registry", relative.as_posix(), f"missing {relative.as_posix()}"))
        return ""


def analyze_registry(root: Path) -> Report:
    descriptors, errors = _parse_registry(root / REGISTRY)
    texts = {dimension: _read(root, path, errors) for dimension, (path, _signature) in C_DIMENSIONS.items()}
    driver_make = _read(root, Path("os/drivers/os_api_test/kernel/Make.defs"), errors)
    wrapper_make = _read(root, Path("apps/examples/testcase/le_tc/kernel/Make.defs"), errors)
    kconfig = _read(root, Path("apps/examples/testcase/le_tc/kernel/Kconfig"), errors)
    for dimension, (_path, signature) in C_DIMENSIONS.items():
        text = texts[dimension]
        adapter_exists = has_c_adapter(text, signature)
        if not adapter_exists:
            errors.append(_issue("registry", dimension, f"missing {dimension} adapter"))
        for descriptor in descriptors:
            if not adapter_exists:
                errors.append(_issue(descriptor.symbol, dimension, f"{descriptor.symbol}: missing {dimension} adapter"))
    seen_symbols: set[str] = set()
    seen_ids: set[int] = set()
    for descriptor in descriptors:
        if descriptor.command_id <= 28 or descriptor.command_id > 255:
            errors.append(_issue(descriptor.symbol, "command-id", f"{descriptor.symbol}: command id must be in 29..255"))
        if descriptor.symbol in seen_symbols or descriptor.command_id in seen_ids:
            errors.append(_issue(descriptor.symbol, "registry", f"{descriptor.symbol}: duplicate symbol or command id"))
        seen_symbols.add(descriptor.symbol)
        seen_ids.add(descriptor.command_id)
        if not make_has_source(driver_make, descriptor.test_gate, descriptor.provider_source):
            errors.append(_issue(descriptor.symbol, "provider-source", f"{descriptor.symbol}: missing provider-source {descriptor.provider_source}"))
        if not make_has_source(wrapper_make, descriptor.test_gate, descriptor.wrapper_source):
            errors.append(_issue(descriptor.symbol, "wrapper-source", f"{descriptor.symbol}: missing wrapper-source {descriptor.wrapper_source}"))
        gate = descriptor.test_gate.removeprefix("CONFIG_")
        if re.search(rf"(?m)^config {re.escape(gate)}$", kconfig) is None:
            errors.append(_issue(descriptor.symbol, "kconfig", f"{descriptor.symbol}: missing kconfig gate {gate}"))
    return {
        "ok": not errors,
        "descriptor_count": len(descriptors),
        "descriptors": [asdict(descriptor) for descriptor in descriptors],
        "errors": errors,
    }


class RegistryContractTest(unittest.TestCase):
    def test_canonical_topology_is_synchronized(self) -> None:
        report = analyze_registry(Path(__file__).resolve().parents[3])
        self.assertTrue(report["ok"], json.dumps(report, indent=2, sort_keys=True))

    def test_core_family_regressions_are_registered_atomically(self) -> None:
        report = analyze_registry(Path(__file__).resolve().parents[3])
        actual = {
            (descriptor["symbol"], descriptor["command_id"], descriptor["provider"], descriptor["wrapper"], descriptor["test_gate"], descriptor["predicate"])
            for descriptor in report["descriptors"]
        }
        self.assertTrue(CORE_FAMILY_REGRESSIONS.issubset(actual), sorted(CORE_FAMILY_REGRESSIONS - actual))

    def test_core_family_dispatch_is_registry_only(self) -> None:
        self.assertEqual(direct_core_dispatch_symbols(Path(__file__).resolve().parents[3]), [])

    def test_complete_synthetic_descriptor_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture(Path(directory), set(C_DIMENSIONS) | {"provider-source", "wrapper-source", "kconfig"})
            self.assertTrue(analyze_registry(Path(directory))["ok"])

    def test_comment_only_c_adapters_are_rejected_for_their_symbol(self) -> None:
        for dimension, (path, _signature) in C_DIMENSIONS.items():
            with self.subTest(dimension=dimension), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                fixture(root, set(C_DIMENSIONS) | {"provider-source", "wrapper-source", "kconfig"})
                adapter = root / path
                adapter.write_text(f"/*\n{adapter.read_text(encoding='utf-8')}*/\n", encoding="utf-8")
                report = analyze_registry(root)
                self.assertFalse(report["ok"])
                self.assertTrue(any(issue["symbol"] == "TESTIOC_SYNTHETIC" and issue["dimension"] == dimension for issue in report["errors"]))

    def test_c_comment_token_splices_are_rejected_like_the_compiler(self) -> None:
        splices = {
            "header": ("symbol = _TESTIOC(id),", "sym/* splice */bol = _TESTIOC(id),"),
            "prototype": ("int provider(int cmd", "int pro/* splice */vider(int cmd"),
            "dispatch": ("case symbol:", "case sym/* splice */bol:"),
            "wrapper": ("wrapper();", "wrap/* splice */per();"),
        }
        for dimension, (path, _signature) in C_DIMENSIONS.items():
            with self.subTest(dimension=dimension), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                fixture(root, set(C_DIMENSIONS) | {"provider-source", "wrapper-source", "kconfig"})
                adapter = root / path
                original, spliced = splices[dimension]
                adapter.write_text(adapter.read_text(encoding="utf-8").replace("#include", "#inc/* splice */lude").replace(original, spliced), encoding="utf-8")
                compiler = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-E", str(adapter)], capture_output=True, text=True, timeout=10, check=False)
                self.assertNotEqual(compiler.returncode, 0)
                report = analyze_registry(root)
                self.assertFalse(report["ok"])
                self.assertTrue(any(issue["symbol"] == "TESTIOC_SYNTHETIC" and issue["dimension"] == dimension for issue in report["errors"]))

    def test_continued_line_comments_hide_adapters_like_the_compiler(self) -> None:
        expansions = {
            "header": "enum { TESTIOC_SYNTHETIC = _TESTIOC(200) };",
            "prototype": "int test_synthetic(int cmd, unsigned long arg);",
            "dispatch": "case TESTIOC_SYNTHETIC: ret = test_synthetic(cmd, arg); break;",
            "wrapper": "synthetic_main();",
        }
        for dimension, (path, _signature) in C_DIMENSIONS.items():
            for newline in ("\n", "\r\n"):
                with self.subTest(dimension=dimension, newline=repr(newline)), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    fixture(root, set(C_DIMENSIONS) | {"provider-source", "wrapper-source", "kconfig"})
                    adapter = root / path
                    active = preprocess(adapter)
                    self.assertEqual(active.returncode, 0, active.stderr)
                    self.assertIn(expansions[dimension], active.stdout)
                    adapter.write_text(adapter.read_text(encoding="utf-8").replace("#include", "// hidden \\" + newline + "#include"), encoding="utf-8")
                    hidden = preprocess(adapter)
                    self.assertNotEqual(hidden.returncode, 0)
                    self.assertNotIn(expansions[dimension], hidden.stdout)
                    report = analyze_registry(root)
                    self.assertFalse(report["ok"])
                    self.assertTrue(any(issue["symbol"] == "TESTIOC_SYNTHETIC" and issue["dimension"] == dimension for issue in report["errors"]))

    def test_comment_only_make_source_rules_are_rejected_for_their_symbol(self) -> None:
        cases = (("provider-source", Path("os/drivers/os_api_test/kernel/Make.defs")), ("wrapper-source", Path("apps/examples/testcase/le_tc/kernel/Make.defs")))
        for dimension, path in cases:
            with self.subTest(dimension=dimension), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                fixture(root, set(C_DIMENSIONS) | {"provider-source", "wrapper-source", "kconfig"})
                makefile = root / path
                makefile.write_text("".join(f"# {line}" for line in makefile.read_text(encoding="utf-8").splitlines(keepends=True)), encoding="utf-8")
                report = analyze_registry(root)
                self.assertFalse(report["ok"])
                self.assertTrue(any(issue["symbol"] == "TESTIOC_SYNTHETIC" and issue["dimension"] == dimension for issue in report["errors"]))

    def test_header_only_descriptor_names_missing_dispatch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture(Path(directory), {"header"})
            report = analyze_registry(Path(directory))
            self.assertIn(_issue("TESTIOC_SYNTHETIC", "dispatch", "TESTIOC_SYNTHETIC: missing dispatch adapter"), report["errors"])

    def test_each_single_dimension_is_rejected_for_its_symbol(self) -> None:
        for dimension in sorted(set(C_DIMENSIONS) | {"provider-source", "wrapper-source", "kconfig"}):
            with self.subTest(dimension=dimension), tempfile.TemporaryDirectory() as directory:
                fixture(Path(directory), {dimension})
                report = analyze_registry(Path(directory))
                self.assertFalse(report["ok"])
                self.assertTrue(any(issue["symbol"] == "TESTIOC_SYNTHETIC" and issue["dimension"] != dimension for issue in report["errors"]))

    def test_malformed_registry_input_is_rejected_at_the_parser(self) -> None:
        cases = (("#if CONFIG_TC_KERNEL_SYNTHETIC\n#endif\n", "malformed predicate"), ("BROKEN(TESTIOC_SYNTHETIC)\n", "malformed registry row"))
        for content, diagnostic in cases:
            with self.subTest(diagnostic=diagnostic), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                fixture(root, set(C_DIMENSIONS) | {"provider-source", "wrapper-source", "kconfig"})
                (root / REGISTRY).write_text(content, encoding="utf-8")
                report = analyze_registry(root)
                self.assertTrue(any(diagnostic in issue["message"] for issue in report["errors"]))

    def test_command_ids_are_bounded_above_the_legacy_range(self) -> None:
        for command_id in (28, 256):
            with self.subTest(command_id=command_id), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                fixture(root, set(C_DIMENSIONS) | {"provider-source", "wrapper-source", "kconfig"})
                registry = root / REGISTRY
                registry.write_text(registry.read_text(encoding="utf-8").replace(", 200,", f", {command_id},"), encoding="utf-8")
                self.assertTrue(any(issue["dimension"] == "command-id" for issue in analyze_registry(root)["errors"]))

    def test_cli_returns_json_and_nonzero_for_incomplete_topology(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture(Path(directory), {"dispatch"})
            process = subprocess.run([sys.executable, __file__, "--root", directory], capture_output=True, text=True, timeout=10, check=False)
            report: Report = json.loads(process.stdout)
            self.assertNotEqual(process.returncode, 0)
            self.assertFalse(report["ok"])
            self.assertTrue(any(issue["symbol"] == "TESTIOC_SYNTHETIC" for issue in report["errors"]))


def main() -> int:
    if len(sys.argv) == 3 and sys.argv[1] == "--root":
        report = analyze_registry(Path(sys.argv[2]).resolve())
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0 if report["ok"] else 1
    unittest.main()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
