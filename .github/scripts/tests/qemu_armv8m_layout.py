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
AB_SCRIPT: Final = Path(".github/scripts/qemu_armv8m_ab.py")
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


def parse_config_list(config: dict[str, str], key: str) -> list[int]:
    value = config.get(key, "").strip('"')
    return [int(item, 0) for item in value.split(",") if item]


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
    board = (root / BOARD).read_text(encoding="utf-8")
    make = (root / MAKE).read_text(encoding="utf-8")
    runner = (root / RUNNER).read_text(encoding="utf-8")
    ab_script = (root / AB_SCRIPT).read_text(encoding="utf-8")
    xip_script = (root / XIP_SCRIPT).read_text(encoding="utf-8")
    kernel_ssram = parse_linker_region(root / KERNEL_SCRIPT, "ssram")
    configs = {name: read_config(root / "build/configs/qemu-armv8m" / name / "defconfig") for name in DEFCONFIGS}
    errors: list[str] = []
    kconfig = (root / KCONFIG).read_text(encoding="utf-8")
    if "range 1 2 if ARCH_BOARD_QEMU_ARMV8M" not in kconfig:
        errors.append("QEMU app-separated NUM_APPS must be range-limited to two")
    if make.count("$(CONFIG_ARCH_BOARD)") < 4:
        errors.append("Make linker paths must derive from CONFIG_ARCH_BOARD")
    if "@python3" in make:
        errors.append("QEMU download command must not pass a literal @ to the shell")
    if "ifeq ($(CONFIG_XIP_KERNEL),y)" not in make or "--config loadable_apps" not in make:
        errors.append("QEMU download command must select loadable_apps for XIP-kernel builds")
    if "} > usram AT > uflash" not in xip_script:
        errors.append("XIP userspace linker must retain RAM VMA with flash LMA")
    if kernel_ssram.origin != 0x10000000 or kernel_ssram.end != 0x10380000:
        errors.append("kernel linker ssram must reserve the upper 512 KiB for heap")
    if "memory-backend-file" not in ab_script or "qemu_armv8m_ab" not in runner:
        errors.append("runner must use the file-backed A/B state image")
    if (
        "binary_manager_check_bootparam_set();" not in board
        or "binary_manager_recover_bootparam_set();" not in board
    ):
        errors.append("QEMU Binary Manager must initialize and recover bootparam state")
    for name, config in configs.items():
        if config.get("CONFIG_ARCH_BOARD") != '"qemu-armv8m"':
            errors.append(f"{name}: board must be qemu-armv8m")
        separated = config.get("CONFIG_APP_BINARY_SEPARATION") == "y"
        if name == "hello" and separated:
            errors.append("hello must remain flat")
        if name != "hello":
            expected_apps = "1" if name == "xip_all" else "2"
            if not separated or config.get("CONFIG_NUM_APPS") != expected_apps:
                errors.append(f"{name}: requires NUM_APPS={expected_apps}")
            if config.get("CONFIG_SUPPORT_COMMON_BINARY") != "y":
                errors.append(f"{name}: common package layout mismatch")
            if config.get("CONFIG_BINARY_MANAGER") != "y":
                errors.append(f"{name}: binary manager must be enabled")
            if config.get("CONFIG_FLASH_PARTITION") != "y":
                errors.append(f"{name}: file-backed flash partitions must be enabled")

            starts = parse_config_list(config, "CONFIG_RAM_KREGIONx_START")
            sizes = parse_config_list(config, "CONFIG_RAM_KREGIONx_SIZE")
            heaps = parse_config_list(config, "CONFIG_RAM_KREGIONx_HEAP_INDEX")
            expected_regions = ([0x80000000, 0x80400000, 0x10380000], [0x400000, 0x800000, 0x80000], [0, 2, 1])
            if (starts, sizes, heaps) != expected_regions:
                errors.append(f"{name}: SRAM/main-RAM heap region mapping is incorrect")
            if config.get("CONFIG_RAM_START") != "0x80400000":
                errors.append(f"{name}: loaded app RAM must start after the kernel reservation")
            if config.get("CONFIG_HEAP_INDEX_LOADED_APP") != "2":
                errors.append(f"{name}: loaded apps must use the main-RAM heap")
            if config.get("CONFIG_RAMMTD_ERASE_ON_INIT") != "n":
                errors.append(f"{name}: file-backed RAMMTD must not erase on init")

            names = [item for item in config.get("CONFIG_FLASH_PART_NAME", "").strip('"').split(",") if item]
            sizes_kib = [int(item) for item in config.get("CONFIG_FLASH_PART_SIZE", "").strip('"').split(",") if item]
            if len(names) != len(sizes_kib) or sum(sizes_kib) != 4096:
                errors.append(f"{name}: A/B flash partition map must fill 4 MiB")
            if names.count("kernel") != 2 or names.count("common") != 2 or names.count("app1") != 2:
                errors.append(f"{name}: kernel/common/app1 must have A/B partitions")
            if name != "xip_all" and names.count("app2") != 2:
                errors.append(f"{name}: app2 must have A/B partitions")
    make_loaders = []
    runner_loaders = [(name, int(address, 0)) for name, address in re.findall(r"loader,file=\{(common|app1)\},addr=(0x[0-9a-fA-F]+)", runner)]
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
