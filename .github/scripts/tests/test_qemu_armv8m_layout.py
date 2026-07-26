#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

from qemu_armv8m_layout import BOARD, CHIP, DEFCONFIGS, KCONFIG, KERNEL_SCRIPT, MAKE, XIP_SCRIPT, LayoutContractError, analyze_layout, inspect_artifact, parse_generated_regions, validate_readelf, write_report


ROOT = Path(__file__).resolve().parents[3]
VALID_PROGRAMS = """\
  LOAD           0x000010 0x102c0010 0x102c0010 0x000100 0x000100 R E 0x1000
  LOAD           0x000110 0x80600000 0x102c0110 0x000080 0x000200 RW  0x1000
"""
VALID_SECTIONS = """\
  [ 1] .text PROGBITS 00000000102c0010 000010 000100 00 AX 0 0 4
  [ 2] .data PROGBITS 0000000080600000 000110 000080 00 WA 0 0 4
"""
LINKER = """\
MEMORY
{
  uflash (rx) : ORIGIN = 0x102c0000, LENGTH = 0x000a0000
  usram (rwx) : ORIGIN = 0x80600000, LENGTH = 0x00100000
}
"""


class QemuArmv8mLayoutContractTest(unittest.TestCase):
    def test_declared_layouts_are_consistent_with_board_authority(self) -> None:
        report = analyze_layout(ROOT)
        self.assertTrue(report["ok"], json.dumps(report, indent=2, sort_keys=True))

    def test_generated_xip_regions_parse(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            linker = Path(directory) / "app1_0.ld"
            linker.write_text(LINKER, encoding="utf-8")
            uflash, usram = parse_generated_regions(linker)
        self.assertEqual((0x102C0000, 0xA0000), (uflash.origin, uflash.length))
        self.assertEqual((0x80600000, 0x100000), (usram.origin, usram.length))

    def test_readelf_accepts_flash_lma_for_ram_data(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            linker = Path(directory) / "common_0.ld"
            linker.write_text(LINKER, encoding="utf-8")
            errors = validate_readelf(VALID_PROGRAMS, VALID_SECTIONS, 0x1000, parse_generated_regions(linker))
        self.assertEqual([], errors)

    def test_readelf_rejects_missing_program_headers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            linker = Path(directory) / "common_0.ld"
            linker.write_text(LINKER, encoding="utf-8")
            with self.assertRaisesRegex(LayoutContractError, "no PT_LOAD"):
                validate_readelf("", "", 0, parse_generated_regions(linker))

    def test_readelf_rejects_vma_lma_file_and_allocated_section_overflows(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            linker = Path(directory) / "app1_0.ld"
            linker.write_text(LINKER, encoding="utf-8")
            regions = parse_generated_regions(linker)
        cases = {
            "VMA": (VALID_PROGRAMS.replace("0x102c0010", "0x20000000", 1), VALID_SECTIONS, 0x1000),
            "LMA": (VALID_PROGRAMS.replace("0x102c0110", "0x20000000"), VALID_SECTIONS, 0x1000),
            "file extent": (VALID_PROGRAMS.replace("0x000110", "0x000fc0"), VALID_SECTIONS, 0x1000),
            "allocated section": (VALID_PROGRAMS, VALID_SECTIONS.replace("00000000102c0010", "0000000020000000"), 0x1000),
        }
        for label, (programs, sections, size) in cases.items():
            with self.subTest(label=label):
                errors = validate_readelf(programs, sections, size, regions)
                self.assertTrue(any(label in error for error in errors), errors)

    def test_contract_rejects_mutated_qemu_inputs(self) -> None:
        mutations = {
            "second app": (Path("build/configs/qemu-armv8m/loadable_all/defconfig"), "CONFIG_NUM_APPS=1", "CONFIG_NUM_APPS=2", "requires exactly app1"),
            "make mismatch": (MAKE, "addr=0x10360000", "addr=0x10360001", "disagrees with board slot"),
            "missing common": (Path("build/configs/qemu-armv8m/xip_all/defconfig"), "CONFIG_SUPPORT_COMMON_BINARY=y", "# CONFIG_SUPPORT_COMMON_BINARY is not set", "common package layout mismatch"),
        }
        for label, (path, before, after, diagnostic) in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                root = self.copy_layout_fixture(Path(directory))
                target = root / path
                target.write_text(target.read_text(encoding="utf-8").replace(before, after), encoding="utf-8")
                self.assertTrue(any(diagnostic in error for error in analyze_layout(root)["errors"]))

    def test_contract_rejects_changed_board_address(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = self.copy_layout_fixture(Path(directory))
            board = root / BOARD
            board.write_text(board.read_text(encoding="utf-8").replace("0x10360000", "0x10361000", 1), encoding="utf-8")
            self.assertTrue(any("disagrees with board slot" in error for error in analyze_layout(root)["errors"]))

    def copy_layout_fixture(self, root: Path) -> Path:
        for relative in (BOARD, CHIP, KCONFIG, MAKE, KERNEL_SCRIPT, XIP_SCRIPT):
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, target)
        for name in DEFCONFIGS:
            relative = Path("build/configs/qemu-armv8m") / name / "defconfig"
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, target)
        return root


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--common-ld", type=Path)
    parser.add_argument("--common-artifact", type=Path)
    parser.add_argument("--app1-ld", type=Path)
    parser.add_argument("--app1-artifact", type=Path)
    args = parser.parse_args(argv[1:])
    report = analyze_layout(args.root.resolve())
    artifact_pairs = (("common", args.common_ld, args.common_artifact), ("app1", args.app1_ld, args.app1_artifact))
    for name, linker, artifact in artifact_pairs:
        if (linker is None) != (artifact is None):
            parser.error(f"--{name}-ld and --{name}-artifact must be provided together")
        if linker is not None and artifact is not None:
            errors = inspect_artifact(linker, artifact)
            report[f"{name}_artifact_errors"] = errors
            report["ok"] = bool(report["ok"]) and not errors
    write_report(args.report, report)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
