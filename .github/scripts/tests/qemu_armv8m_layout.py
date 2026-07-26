#!/usr/bin/env python3

from __future__ import annotations

import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Final, TypeAlias


BOARD: Final = Path("os/board/qemu-armv8m/src/qemu_armv8m_boot.c")
CHIP: Final = Path("os/arch/arm/include/qemu-armv8m/chip.h")
KCONFIG: Final = Path("os/Kconfig")
MAKE: Final = Path("build/configs/qemu-armv8m/Make.defs")
RUNNER: Final = Path(".github/scripts/qemu-armv8m-kernel-tc.py")
KERNEL_SCRIPT: Final = Path("build/configs/qemu-armv8m/scripts/mps2-an505.ld")
XIP_SCRIPT: Final = Path("build/configs/qemu-armv8m/scripts/xipelf/userspace_all.ld")
DEFCONFIGS: Final = ("hello", "loadable_all", "loadable_apps", "xip_all")
NUMBER: Final = r"0x[0-9a-fA-F]+|[0-9]+"
ReportValue: TypeAlias = bool | list[str] | list[tuple[str, int]] | dict[str, dict[str, str | int]]


class LayoutContractError(ValueError):
    def __init__(self, detail: str) -> None:
        super().__init__(detail)


@dataclass(frozen=True, slots=True)
class Region:
    name: str
    origin: int
    length: int

    @property
    def end(self) -> int:
        return self.origin + self.length

    def contains(self, start: int, size: int) -> bool:
        return size >= 0 and self.origin <= start and start + size <= self.end


def read_config(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            values[line[2:-11]] = "n"
        elif line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return values


def parse_define(text: str, name: str) -> list[int]:
    return [int(value, 0) for value in re.findall(rf"^#define {name}\s+({NUMBER})", text, re.MULTILINE)]


def parse_board_slots(root: Path) -> dict[str, Region]:
    board = (root / BOARD).read_text(encoding="utf-8")
    chip = (root / CHIP).read_text(encoding="utf-8")
    common = parse_define(board, "QEMU_COMMON_LOADADDR")
    app1 = parse_define(board, "QEMU_APP1_LOADADDR")
    ssram_base = parse_define(chip, "MPS2_AN505_SSRAM_BASE")
    ssram_size = parse_define(chip, "MPS2_AN505_SSRAM_SIZE")
    if len(common) != 1 or len(app1) != 2:
        raise LayoutContractError("board package slot definitions are malformed")
    if len(ssram_base) != 1 or len(ssram_size) != 1:
        raise LayoutContractError("MPS2 AN505 SSRAM definition is malformed")
    xip_app1, loadable_app1 = app1
    ssram_end = ssram_base[0] + ssram_size[0]
    if not ssram_base[0] <= common[0] < xip_app1 < ssram_end or not ssram_base[0] <= loadable_app1 < ssram_end:
        raise LayoutContractError("board package slot boundaries are malformed")
    return {
        "loadable-app1": Region("loadable-app1", loadable_app1, ssram_end - loadable_app1),
        "xip-common": Region("xip-common", common[0], xip_app1 - common[0]),
        "xip-app1": Region("xip-app1", xip_app1, ssram_end - xip_app1),
    }


def parse_generated_regions(path: Path) -> tuple[Region, Region]:
    text = path.read_text(encoding="utf-8")
    matches = re.findall(rf"\b(uflash|usram)\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*({NUMBER})\s*,\s*LENGTH\s*=\s*({NUMBER})", text)
    regions = {name: Region(name, int(origin, 0), int(length, 0)) for name, origin, length in matches}
    if set(regions) != {"uflash", "usram"}:
        raise LayoutContractError(f"missing uflash/usram regions in {path}")
    return regions["uflash"], regions["usram"]


def parse_linker_region(path: Path, name: str) -> Region:
    text = path.read_text(encoding="utf-8")
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*({NUMBER})\s*,\s*LENGTH\s*=\s*({NUMBER})", text)
    if match is None:
        raise LayoutContractError(f"missing {name} region in {path}")
    return Region(name, int(match.group(1), 0), int(match.group(2), 0))


def parse_readelf(programs: str, sections: str) -> tuple[list[tuple[int, int, int, int, int]], list[tuple[int, int]]]:
    loads: list[tuple[int, int, int, int, int]] = []
    for offset, vaddr, paddr, filesz, memsz in re.findall(rf"^\s*LOAD\s+({NUMBER})\s+({NUMBER})\s+({NUMBER})\s+({NUMBER})\s+({NUMBER})", programs, re.MULTILINE):
        loads.append((int(offset, 0), int(vaddr, 0), int(paddr, 0), int(filesz, 0), int(memsz, 0)))
    allocated: list[tuple[int, int]] = []
    section = re.compile(r"^\s*\[\s*\d+\]\s+\S+\s+\S+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+([A-Z]+)", re.MULTILINE)
    for address, _offset, size, flags in section.findall(sections):
        if "A" in flags:
            allocated.append((int(address, 16), int(size, 16)))
    if not loads:
        raise LayoutContractError("readelf output contains no PT_LOAD program header")
    return loads, allocated


def validate_readelf(programs: str, sections: str, artifact_size: int, regions: tuple[Region, Region]) -> list[str]:
    loads, allocated = parse_readelf(programs, sections)
    errors: list[str] = []
    for offset, vaddr, paddr, filesz, memsz in loads:
        if not any(region.contains(vaddr, memsz) for region in regions):
            errors.append(f"PT_LOAD VMA 0x{vaddr:x}+0x{memsz:x} outside uflash/usram")
        if not any(region.contains(paddr, filesz) for region in regions):
            errors.append(f"PT_LOAD LMA 0x{paddr:x}+0x{filesz:x} outside uflash/usram")
        if offset + filesz > artifact_size:
            errors.append(f"PT_LOAD file extent 0x{offset:x}+0x{filesz:x} exceeds artifact")
    for address, size in allocated:
        if not any(region.contains(address, size) for region in regions):
            errors.append(f"allocated section 0x{address:x}+0x{size:x} outside uflash/usram")
    return errors


def analyze_layout(root: Path) -> dict[str, ReportValue]:
    slots = parse_board_slots(root)
    make = (root / MAKE).read_text(encoding="utf-8")
    runner = (root / RUNNER).read_text(encoding="utf-8")
    xip_script = (root / XIP_SCRIPT).read_text(encoding="utf-8")
    kernel_ssram = parse_linker_region(root / KERNEL_SCRIPT, "ssram")
    configs = {name: read_config(root / "build/configs/qemu-armv8m" / name / "defconfig") for name in DEFCONFIGS}
    make_loaders = [(name, int(address, 0)) for name, address in re.findall(r"loader,file=\$\(TOPDIR\)/\.\./build/output/bin/(common|app1),addr=(0x[0-9a-fA-F]+)", make)]
    runner_loaders = [(name, int(address, 0)) for name, address in re.findall(r"loader,file=\{(common|app1)\},addr=(0x[0-9a-fA-F]+)", runner)]
    expected = {"common": {slots["xip-common"].origin}, "app1": {slots["loadable-app1"].origin, slots["xip-app1"].origin}}
    errors: list[str] = []
    if "range 1 1 if ARCH_BOARD_QEMU_ARMV8M" not in (root / KCONFIG).read_text(encoding="utf-8"):
        errors.append("QEMU app-separated NUM_APPS must be range-limited to one")
    if make.count("$(CONFIG_ARCH_BOARD)") < 4:
        errors.append("Make linker paths must derive from CONFIG_ARCH_BOARD")
    if "} > usram AT > uflash" not in xip_script:
        errors.append("XIP userspace linker must retain RAM VMA with flash LMA")
    if kernel_ssram.end != slots["xip-common"].origin:
        errors.append("kernel linker ssram must end at the common package slot")
    for name, config in configs.items():
        if config.get("CONFIG_ARCH_BOARD") != '"qemu-armv8m"':
            errors.append(f"{name}: board must be qemu-armv8m")
        separated = config.get("CONFIG_APP_BINARY_SEPARATION") == "y"
        if name == "hello" and separated:
            errors.append("hello must remain flat")
        if name != "hello" and (not separated or config.get("CONFIG_NUM_APPS") != "1" or config.get("CONFIG_APP1_BIN_NAME") != '"app1"'):
            errors.append(f"{name}: requires exactly app1")
        common = config.get("CONFIG_SUPPORT_COMMON_BINARY") == "y"
        if (name == "xip_all") != common:
            errors.append(f"{name}: common package layout mismatch")
    for name, address in make_loaders:
        if address not in expected[name]:
            errors.append(f"{name}: loader address 0x{address:x} disagrees with board slot")
    for name, address in runner_loaders:
        if address not in expected[name]:
            errors.append(f"runner {name}: loader address 0x{address:x} disagrees with board slot")
    if make_loaders != [("common", slots["xip-common"].origin), ("app1", slots["xip-app1"].origin), ("app1", slots["loadable-app1"].origin)]:
        errors.append("Make loader layout is not xip common+app1 followed by loadable app1")
    if runner_loaders != [("app1", slots["loadable-app1"].origin), ("common", slots["xip-common"].origin), ("app1", slots["xip-app1"].origin)]:
        errors.append("runner loader layout is not loadable app1 and xip common+app1")
    slot_report = {name: {"name": slot.name, "origin": slot.origin, "length": slot.length} for name, slot in slots.items()}
    return {"ok": not errors, "errors": errors, "slots": slot_report, "make_loaders": make_loaders, "runner_loaders": runner_loaders}


def inspect_artifact(linker: Path, artifact: Path) -> list[str]:
    regions = parse_generated_regions(linker)
    programs = subprocess.run(["readelf", "-lW", artifact], capture_output=True, text=True, timeout=10, check=False)
    sections = subprocess.run(["readelf", "-SW", artifact], capture_output=True, text=True, timeout=10, check=False)
    if programs.returncode != 0 or sections.returncode != 0:
        return [f"readelf failed for {artifact}"]
    return validate_readelf(programs.stdout, sections.stdout, artifact.stat().st_size, regions)


def write_report(path: Path, report: dict[str, ReportValue]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
