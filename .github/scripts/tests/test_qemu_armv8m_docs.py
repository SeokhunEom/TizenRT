#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
# ─── How to run ───
# python3 .github/scripts/tests/test_qemu_armv8m_docs.py -v

from __future__ import annotations

import argparse
import dataclasses
import json
import re
import sys
import unittest
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Callable, Final, Sequence


ROOT: Final = Path(__file__).resolve().parents[3]
CONFIGS: Final = ("hello", "loadable_all", "loadable_apps", "xip_all")
RUNNER: Final = "python3 .github/scripts/qemu-armv8m-kernel-tc.py"
FULL_SHA: Final = re.compile(r"[0-9a-f]{40}")
LEGACY_QEMU_COMMAND: Final = "-kernel ../build/output/bin/tinyara -nographic -gdb tcp::3333"


@dataclass(frozen=True, slots=True)
class Documentation:
    english: str
    korean: str
    targets: str
    legacy: str


def section(text: str, heading: str) -> str:
    start = text.find(heading)
    if start < 0:
        return ""
    remaining = text[start + len(heading):]
    next_heading = re.search(r"^## ", remaining, re.MULTILINE)
    if next_heading is None:
        return remaining
    return remaining[:next_heading.start()]


def table_configs(text: str, heading: str) -> tuple[str, ...]:
    body = section(text, heading)
    return tuple(re.findall(r"^\| `([a-z0-9_]+)` \|", body, re.MULTILINE))


def documentation() -> Documentation:
    config_root = ROOT / "build" / "configs"
    armv8m_root = config_root / "qemu-armv8m"
    return Documentation(
        english=(armv8m_root / "README.md").read_text(encoding="utf-8"),
        korean=(armv8m_root / "READMD_KOR.md").read_text(encoding="utf-8"),
        targets=(config_root / "qemu-targets.md").read_text(encoding="utf-8"),
        legacy=(config_root / "qemu" / "README.md").read_text(encoding="utf-8"),
    )


def mutate_copied_document(contents: str, mutate: Callable[[str], str]) -> str:
    with TemporaryDirectory(prefix="qemu-armv8m-docs-") as directory:
        copied = Path(directory) / "README.md"
        copied.write_text(contents, encoding="utf-8")
        copied.write_text(mutate(copied.read_text(encoding="utf-8")), encoding="utf-8")
        return copied.read_text(encoding="utf-8")


def analyze_docs(docs: Documentation) -> list[str]:
    errors: list[str] = []
    if table_configs(docs.english, "## Supported configurations") != CONFIGS:
        errors.append("English supported-config table must contain exactly the four runner configurations")
    if table_configs(docs.korean, "## 지원하는 네 가지 config") != CONFIGS:
        errors.append("Korean supported-config table must contain exactly the four runner configurations")

    for config in CONFIGS:
        invocation = f"{RUNNER} --config {config} --timeout 1200"
        if invocation not in docs.english:
            errors.append(f"documentation must direct {config} runtime validation to the tested runner")

    armv8m_text = docs.english + docs.korean + section(docs.targets, "## New `qemu-armv8m`")
    for forbidden in ("qemu-system-arm", "-device loader", "addr=0x", "-kernel ../build/output"):
        if forbidden in armv8m_text:
            errors.append(f"ARMv8-M documentation must not duplicate raw QEMU command/address: {forbidden}")
    for stale_claim in (
        "runtime fault",
        "IRQ assertion",
        "런타임 fault",
        "semaphore-holder assertion caused a timeout",
        "full Kernel TC is not currently green",
        "semaphore holder assertion으로 timeout",
    ):
        if stale_claim in armv8m_text:
            errors.append(f"documentation must not retain the unverified missing-common fault claim: {stale_claim}")

    required_english = (
        "PASS > 0",
        "FAIL : 0",
        '"status": "pass"',
        "app1 and app2 are supported by loadable configurations",
        "common before app1",
        "binary_manager_load: Invalid Header data, name : common",
        "binary_manager_load: Invalid Header data, name : app1",
        "binary_manager_load: common Header Checking Success",
        "binary_manager_load: app1 Header Checking Success",
        "--expect-reject",
        "--forbid-marker",
        "Local runtime evidence includes `hello`, `loadable_all`, `loadable_apps`, and",
        "PASS : 459, FAIL : 0",
        "PASS : 447, FAIL : 0",
        "preallocated holder pool is a",
        "local QEMU software-path evidence, not hardware-board validation",
        "explicit candidate commit/push authorization",
        ".github/workflows/qemu-armv8m.yml",
        "ubuntu-24.04",
        "tizenrt/tizenrt@sha256:",
        "full commit SHA",
        "build/qemu-armv8m/ci-artifacts/<config>/",
        "negative-<case>",
        "serial.log",
        "result.json",
        "xip-layout-report.json",
    )
    for required in required_english:
        if required not in docs.english:
            errors.append(f"English ARMv8-M documentation is missing contract detail: {required}")

    for mutable_claim in ("ubuntu-latest", "tizenrt/tizenrt:latest", "tizenrt/tizenrt:1.5.8"):
        if mutable_claim in armv8m_text:
            errors.append(f"documentation must not claim mutable CI input: {mutable_claim}")
    for action in re.findall(r"actions/[A-Za-z0-9_-]+@[A-Za-z0-9_-]+", armv8m_text):
        if FULL_SHA.fullmatch(action.rsplit("@", 1)[1]) is None:
            errors.append(f"documentation action reference must use a full commit SHA: {action}")

    target_section = section(docs.targets, "## New `qemu-armv8m`")
    if RUNNER not in target_section:
        errors.append("QEMU target roles must direct ARMv8-M execution to the tested runner")
    if "lm3s6963" in (armv8m_text + docs.legacy).lower():
        errors.append("legacy QEMU documentation must spell LM3S6965 consistently")
    if "an expected rejection is not a successful application\nlaunch" not in docs.english:
        errors.append("expected-rejection contract must not describe rejection as a successful positive execution")
    if '"status": "expected-rejection"' not in docs.english:
        errors.append("expected-rejection contract must name the runner result status")
    if re.search(r"PASS\s*:\s*0\s+with\s+FAIL\s*:\s*0\s+is\s+a\s+positive", docs.english, re.IGNORECASE):
        errors.append("positive runner contract must not classify PASS : 0 with FAIL : 0 as a pass")
    if LEGACY_QEMU_COMMAND not in docs.legacy:
        errors.append("legacy qemu command must retain the reviewed tinyara kernel path")
    if (ROOT / "build/configs/qemu-armv8m/console_duplicate_candidates.md").exists():
        errors.append("unverified console candidate document must not be delivered")
    return errors


def write_report(path: Path, errors: list[str]) -> None:
    report = {"errors": errors, "ok": not errors}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


class QemuArmv8mDocumentationTest(unittest.TestCase):
    def test_current_docs_satisfy_the_runner_contract(self) -> None:
        self.assertEqual([], analyze_docs(documentation()))

    def test_rejects_fifth_configuration(self) -> None:
        docs = documentation()
        english = mutate_copied_document(
            docs.english,
            lambda contents: contents.replace(
                "\n## Build", "\n| `app2` | Unsupported | Must not be documented |\n\n## Build", 1
            ),
        )
        self.assertTrue(
            any("English supported-config table" in error for error in analyze_docs(dataclasses.replace(docs, english=english)))
        )

    def test_rejects_stale_missing_common_fault_claim(self) -> None:
        docs = documentation()
        english = mutate_copied_document(
            docs.english, lambda contents: contents + "\nA missing common package causes a runtime fault.\n"
        )
        self.assertTrue(
            any("missing-common fault claim" in error for error in analyze_docs(dataclasses.replace(docs, english=english)))
        )

    def test_rejects_raw_qemu_command_or_address(self) -> None:
        docs = documentation()
        english = mutate_copied_document(
            docs.english, lambda contents: contents + "\nqemu-system-arm -device loader,addr=0x10300000\n"
        )
        self.assertTrue(
            any("raw QEMU command/address" in error for error in analyze_docs(dataclasses.replace(docs, english=english)))
        )

    def test_rejects_mutable_ci_claim(self) -> None:
        docs = documentation()
        english = mutate_copied_document(
            docs.english, lambda contents: contents + "\nCI uses ubuntu-latest with tizenrt/tizenrt:latest.\n"
        )
        self.assertTrue(
            any("mutable CI input" in error for error in analyze_docs(dataclasses.replace(docs, english=english)))
        )

    def test_rejects_lm3s6963_typo(self) -> None:
        docs = documentation()
        legacy = mutate_copied_document(docs.legacy, lambda contents: contents.replace("lm3s6965", "lm3s6963", 1))
        self.assertIn("LM3S6965", "\n".join(analyze_docs(dataclasses.replace(docs, legacy=legacy))))

    def test_rejects_mutable_action_reference(self) -> None:
        docs = documentation()
        english = mutate_copied_document(
            docs.english, lambda contents: contents + "\nCI uses actions/checkout@v4.\n"
        )
        self.assertTrue(
            any("action reference" in error for error in analyze_docs(dataclasses.replace(docs, english=english)))
        )

    def test_rejects_lm3s6963_in_armv8m_readme(self) -> None:
        docs = documentation()
        english = mutate_copied_document(docs.english, lambda contents: contents.replace("LM3S6965", "LM3S6963", 1))
        self.assertTrue(
            any("LM3S6965" in error for error in analyze_docs(dataclasses.replace(docs, english=english)))
        )

    def test_rejects_expected_rejection_claimed_as_positive_execution(self) -> None:
        docs = documentation()
        english = mutate_copied_document(
            docs.english,
            lambda contents: contents.replace(
                "an expected rejection is not a successful application\nlaunch",
                "an expected rejection is a successful positive execution",
                1,
            ),
        )
        self.assertTrue(
            any("expected-rejection contract" in error for error in analyze_docs(dataclasses.replace(docs, english=english)))
        )

    def test_rejects_pass_zero_claimed_as_positive(self) -> None:
        docs = documentation()
        english = mutate_copied_document(
            docs.english, lambda contents: contents + "\nPASS : 0 with FAIL : 0 is a positive run.\n"
        )
        self.assertTrue(
            any("PASS : 0" in error for error in analyze_docs(dataclasses.replace(docs, english=english)))
        )

    def test_rejects_legacy_tinyara_bin_command(self) -> None:
        docs = documentation()
        legacy = mutate_copied_document(
            docs.legacy,
            lambda contents: contents.replace(
                "-kernel ../build/output/bin/tinyara -nographic -gdb tcp::3333",
                "-kernel ../build/output/bin/tinyara.bin -nographic -gdb tcp::3333",
                1,
            ),
        )
        self.assertTrue(
            any("legacy qemu command" in error for error in analyze_docs(dataclasses.replace(docs, legacy=legacy)))
        )


def main(arguments: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--report", type=Path)
    options, unittest_arguments = parser.parse_known_args(arguments)
    if options.report is not None:
        errors = analyze_docs(documentation())
        write_report(options.report, errors)
        print(json.dumps({"errors": errors, "ok": not errors}, indent=2, sort_keys=True))
        return 0 if not errors else 1
    unittest.main(argv=[sys.argv[0], *unittest_arguments])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
