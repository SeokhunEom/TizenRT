from __future__ import annotations

import struct
import tempfile
import zlib
from pathlib import Path


RAM_BASE = 0x80000000
RAM_SIZE = 16 * 1024 * 1024
BOOTPARAM_SIZE = 8192
BOOTPARAM_SLOT_SIZE = 4096
BP_VERSION_OFFSET = 4
BP_FORMAT_OFFSET = 8
BP_ACTIVE_IDX_OFFSET = 12
BP_KERNEL_ADDRESS_OFFSET = 13
BP_APP_COUNT_OFFSET = 21
BP_APP_DATA_OFFSET = 22
BP_APP_DATA_STRIDE = 17
BP_APP_USEIDX_OFFSET = 16
BP_FORMAT_VERSION = 2


class AbImageError(ValueError):
    pass


def _read_config_file(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            values[line[2:-11]] = "n"
        elif line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value.strip('"')
    return values


def _matches_config(values: dict[str, str], config: str) -> bool:
    expected = {
        "hello": {
            "CONFIG_APP_BINARY_SEPARATION": "n",
            "CONFIG_XIP_KERNEL": "n",
            "CONFIG_XIP_ELF": "n",
        },
        "loadable_all": {
            "CONFIG_APP_BINARY_SEPARATION": "y",
            "CONFIG_XIP_KERNEL": "n",
            "CONFIG_XIP_ELF": "n",
        },
        "loadable_apps": {
            "CONFIG_APP_BINARY_SEPARATION": "y",
            "CONFIG_XIP_KERNEL": "y",
            "CONFIG_XIP_ELF": "n",
        },
        "xip_all": {
            "CONFIG_APP_BINARY_SEPARATION": "y",
            "CONFIG_XIP_ELF": "y",
        },
    }.get(config)
    if expected is None or values.get("CONFIG_ARCH_BOARD") != "qemu-armv8m":
        return False
    return all(values.get(key, "n") == value for key, value in expected.items())


def read_config(root: Path, config: str) -> dict[str, str]:
    generated = root / "os" / ".config"
    if generated.is_file():
        values = _read_config_file(generated)
        if _matches_config(values, config):
            return values

    path = root / "build" / "configs" / "qemu-armv8m" / config / "defconfig"
    return _read_config_file(path)


def partition_layout(root: Path, config: str) -> dict[str, list[tuple[int, int]]]:
    values = read_config(root, config)
    start = int(values["CONFIG_FLASH_START_ADDR"], 0)
    names = [item for item in values["CONFIG_FLASH_PART_NAME"].split(",") if item]
    sizes = [int(item) * 1024 for item in values["CONFIG_FLASH_PART_SIZE"].split(",") if item]
    if len(names) != len(sizes):
        raise AbImageError("partition names and sizes have different lengths")

    result: dict[str, list[tuple[int, int]]] = {}
    address = start
    for name, size in zip(names, sizes):
        result.setdefault(name, []).append((address, size))
        address += size
    if address != start + int(values["CONFIG_FLASH_SIZE"]):
        raise AbImageError("partition layout does not fill CONFIG_FLASH_SIZE")
    return result


def _package_payload(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) < 16:
        raise AbImageError(f"kernel package is too small: {path}")
    header_size = struct.unpack_from("<H", data, 4)[0]
    binary_size = struct.unpack_from("<I", data, 10)[0]
    payload_start = 4 + header_size
    payload_end = payload_start + binary_size
    if payload_start < 16 or payload_end > len(data):
        raise AbImageError(f"invalid kernel package header: {path}")
    return data[payload_start:payload_end]


def _put_package(image: bytearray, address: int, package: Path, size: int) -> None:
    data = package.read_bytes()
    offset = address - RAM_BASE
    if offset < 0 or offset + len(data) > len(image) or len(data) > size:
        raise AbImageError(f"package does not fit partition: {package}")
    image[offset:offset + len(data)] = data


def _bootparam(names: list[str], kernel_addresses: list[int]) -> bytes:
    first = bytearray([0xff] * BOOTPARAM_SLOT_SIZE)
    struct.pack_into("<III", first, 4, 1, 2, 0)
    for index, address in enumerate(kernel_addresses[:2]):
        struct.pack_into("<I", first, 13 + index * 4, address)

    app_names = [name for name in names if name in ("common", "app1", "app2")]
    first[21] = len(app_names)
    cursor = 22
    for name in app_names:
        encoded = name.encode("ascii")[:15]
        first[cursor:cursor + 16] = encoded + b"\0" * (16 - len(encoded))
        first[cursor + 16] = 0
        cursor += 17

    first[-1] = 0
    struct.pack_into("<I", first, 0, zlib.crc32(first[4:]) & 0xffffffff)
    return bytes(first) + bytes([0xff] * BOOTPARAM_SLOT_SIZE)


def _write_at(image: bytearray, address: int, data: bytes) -> None:
    offset = address - RAM_BASE
    if offset < 0 or offset + len(data) > len(image):
        raise AbImageError("data is outside QEMU main RAM")
    image[offset:offset + len(data)] = data


def stage_state(
    root: Path,
    config: str,
    state: Path,
    *,
    force: bool = False,
    common_path: Path | None = None,
    app1_path: Path | None = None,
    omit_common: bool = False,
) -> None:
    layout = partition_layout(root, config)
    values = read_config(root, config)
    bin_dir = root / "build" / "output" / "bin"
    state.parent.mkdir(parents=True, exist_ok=True)
    if state.exists() and not force:
        if state.stat().st_size != RAM_SIZE:
            raise AbImageError(f"state image must be exactly {RAM_SIZE} bytes: {state}")
        return

    image = bytearray([0] * RAM_SIZE)
    flash_start = int(values["CONFIG_FLASH_START_ADDR"], 0)
    flash_size = int(values["CONFIG_FLASH_SIZE"])
    image[flash_start - RAM_BASE:flash_start - RAM_BASE + flash_size] = b"\xff" * flash_size

    kernel = bin_dir / "tinyara.bin"
    if not kernel.is_file():
        raise FileNotFoundError(f"missing packaged kernel: {kernel}")
    kernel_partitions = layout.get("kernel", [])
    if len(kernel_partitions) != 2:
        raise AbImageError("A/B state requires two kernel partitions")
    raw_kernel = _package_payload(kernel)
    for address, size in kernel_partitions:
        _put_package(image, address, kernel, size)

    configured_names = [name for name in ("common", "app1", "app2") if name in layout]
    overrides = {"common": common_path, "app1": app1_path}
    for name in configured_names:
        if name == "common" and omit_common:
            continue
        package = overrides.get(name) or bin_dir / name
        alternate = bin_dir / f"{name}_1"
        if not package.is_file():
            raise FileNotFoundError(f"missing package: {package}")
        slots = layout[name]
        if len(slots) != 2:
            raise AbImageError(f"A/B state requires two {name} partitions")
        _put_package(image, slots[0][0], package, slots[0][1])
        _put_package(image, slots[1][0], alternate if alternate.is_file() else package, slots[1][1])

    bp_address = next(iter(layout["bootparam"]))[0]
    bootparam = bin_dir / "bootparam.bin"
    if bootparam.is_file() and bootparam.stat().st_size == BOOTPARAM_SIZE:
        bp_data = bootparam.read_bytes()
    else:
        bp_data = _bootparam(configured_names, [address for address, _ in kernel_partitions])
    _write_at(image, bp_address, bp_data)
    state.write_bytes(image)

    raw_kernel_path = state.with_suffix(".kernel.bin")
    raw_kernel_path.write_bytes(raw_kernel)


def _valid_bootparams(
    image: bytes,
    offset: int,
    kernel_addresses: list[int],
    app_count: int,
) -> list[tuple[int, int, bytes]]:
    valid: list[tuple[int, int, bytes]] = []
    for bp_index in range(2):
        start = offset + bp_index * BOOTPARAM_SLOT_SIZE
        candidate = image[start:start + BOOTPARAM_SLOT_SIZE]
        if len(candidate) != BOOTPARAM_SLOT_SIZE:
            continue
        checksum = struct.unpack_from("<I", candidate, 0)[0]
        if checksum == zlib.crc32(candidate[4:]) & 0xffffffff:
            version = struct.unpack_from("<I", candidate, BP_VERSION_OFFSET)[0]
            format_version = struct.unpack_from("<I", candidate, BP_FORMAT_OFFSET)[0]
            active_idx = candidate[BP_ACTIVE_IDX_OFFSET]
            addresses = [
                struct.unpack_from("<I", candidate, BP_KERNEL_ADDRESS_OFFSET + index * 4)[0]
                for index in range(2)
            ]
            if (
                format_version != BP_FORMAT_VERSION
                or active_idx not in (0, 1)
                or addresses != kernel_addresses
                or candidate[BP_APP_COUNT_OFFSET] != app_count
            ):
                continue
            if any(
                candidate[BP_APP_DATA_OFFSET + index * BP_APP_DATA_STRIDE + BP_APP_USEIDX_OFFSET]
                not in (0, 1)
                for index in range(app_count)
            ):
                continue
            valid.append((bp_index, version, candidate))
    return valid


def active_slot(state: Path, config_root: Path, config: str) -> int:
    layout = partition_layout(config_root, config)
    bp_address = next(iter(layout["bootparam"]))[0]
    image = state.read_bytes()
    offset = bp_address - RAM_BASE
    kernel_addresses = [address for address, _ in layout["kernel"]]
    app_count = sum(name in layout for name in ("common", "app1", "app2"))
    valid = _valid_bootparams(image, offset, kernel_addresses, app_count)
    if not valid:
        raise AbImageError("no valid bootparam slot")
    return max(valid, key=lambda item: item[1])[2][BP_ACTIVE_IDX_OFFSET] & 1


def extract_active_kernel(root: Path, config: str, state: Path) -> Path:
    layout = partition_layout(root, config)
    slot = active_slot(state, root, config)
    address, size = layout["kernel"][slot]
    image = state.read_bytes()
    offset = address - RAM_BASE
    package = image[offset:offset + size]
    path = state.with_suffix(".kernel.bin")
    path.write_bytes(_package_payload_from_bytes(package))
    return path


def _package_payload_from_bytes(data: bytes) -> bytes:
    if len(data) < 16:
        raise AbImageError("kernel partition is too small")
    header_size = struct.unpack_from("<H", data, 4)[0]
    binary_size = struct.unpack_from("<I", data, 10)[0]
    start = 4 + header_size
    end = start + binary_size
    if start < 16 or end > len(data):
        raise AbImageError("invalid kernel package in state image")
    return data[start:end]


def flip_active_slot(state: Path, config_root: Path, config: str) -> None:
    image = bytearray(state.read_bytes())
    if len(image) != RAM_SIZE:
        raise AbImageError("invalid state image size")
    layout = partition_layout(config_root, config)
    address = next(iter(layout["bootparam"]))[0]
    offset = address - RAM_BASE
    kernel_addresses = [address for address, _ in layout["kernel"]]
    app_count = sum(name in layout for name in ("common", "app1", "app2"))
    valid = _valid_bootparams(image, offset, kernel_addresses, app_count)
    if not valid:
        raise AbImageError("no valid bootparam slot to flip")

    current_bp, version, current = max(valid, key=lambda item: item[1])
    candidate = bytearray(current)
    active_idx = candidate[BP_ACTIVE_IDX_OFFSET] ^ 1
    candidate[BP_ACTIVE_IDX_OFFSET] = active_idx
    app_count = candidate[BP_APP_COUNT_OFFSET]
    for app_index in range(app_count):
        useidx = BP_APP_DATA_OFFSET + app_index * BP_APP_DATA_STRIDE + BP_APP_USEIDX_OFFSET
        if useidx >= BOOTPARAM_SLOT_SIZE:
            raise AbImageError("bootparam app data exceeds slot")
        candidate[useidx] = active_idx
    values = read_config(config_root, config)
    if values.get("CONFIG_RESOURCE_FS") == "y":
        resource_idx = BP_APP_DATA_OFFSET + app_count * BP_APP_DATA_STRIDE
        if resource_idx >= BOOTPARAM_SLOT_SIZE:
            raise AbImageError("bootparam resource data exceeds slot")
        candidate[resource_idx] = active_idx
    struct.pack_into("<I", candidate, BP_VERSION_OFFSET, version + 1)
    struct.pack_into("<I", candidate, 0, zlib.crc32(candidate[4:]) & 0xffffffff)

    target_bp = current_bp ^ 1
    start = offset + target_bp * BOOTPARAM_SLOT_SIZE
    image[start:start + BOOTPARAM_SLOT_SIZE] = candidate
    state.write_bytes(image)


def qemu_command(state: Path, kernel: Path) -> list[str]:
    return [
        "qemu-system-arm",
        "-M", "mps2-an505,memory-backend=qemu_ram",
        "-object", f"memory-backend-file,id=qemu_ram,size={RAM_SIZE},mem-path={state},share=on",
        "-device", f"loader,file={kernel},addr=0x10000000,force-raw=on",
        "-no-reboot",
        "-display", "none",
        "-serial", "stdio",
        "-monitor", "none",
    ]


def temporary_state() -> tempfile.TemporaryDirectory[str]:
    return tempfile.TemporaryDirectory(prefix="qemu-armv8m-ab-")
