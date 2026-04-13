#!/usr/bin/env bash
#============================================================================
#  Copyright 2026 Samsung Electronics All Rights Reserved.
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#============================================================================

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -f "${SCRIPT_DIR}/../../../../os/.config" ] && [ -f "${SCRIPT_DIR}/../../../../tools/qemu_test/Dockerfile" ]; then
	TOPDIR=$(cd "${SCRIPT_DIR}/../../../.." && pwd)
elif [ -f "${PWD}/os/.config" ] && [ -f "${PWD}/tools/qemu_test/Dockerfile" ]; then
	TOPDIR=$(cd "${PWD}" && pwd)
else
	echo "ERROR: failed to resolve TizenRT topdir"
	exit 1
fi

RUNTIME_IMAGE=${QEMU_RUNTIME_DOCKER_IMAGE:-${QEMU_VIRT_DOCKER_IMAGE:-tizenrt-qemu-test}}
CONFIG_FILE="${TOPDIR}/os/.config"
IMAGE_DIR="${TOPDIR}/qemu_mps2_an505_images"

config_enabled() {
	grep -q "^$1=y" "${CONFIG_FILE}"
}

config_value() {
	grep "^$1=" "${CONFIG_FILE}" | head -n 1 | cut -d= -f2-
}

if [ -t 0 ] && [ -t 1 ]; then
	DOCKER_TTY_FLAGS="-it"
else
	DOCKER_TTY_FLAGS="-i"
fi

if [ ! -f "${CONFIG_FILE}" ]; then
	echo "ERROR: ${CONFIG_FILE} not found"
	exit 1
fi

if [ ! -f "${TOPDIR}/tools/qemu_test/Dockerfile" ]; then
	echo "ERROR: runtime Dockerfile missing"
	exit 1
fi

if [ -z "$(docker images -q "${RUNTIME_IMAGE}" 2> /dev/null)" ]; then
	docker build -t "${RUNTIME_IMAGE}" -f "${TOPDIR}/tools/qemu_test/Dockerfile" "${TOPDIR}"
fi

if [ -f "${IMAGE_DIR}/kernel-runtime.elf" ]; then
	KERNEL_ARG="/workspace/qemu_mps2_an505_images/kernel-runtime.elf"
elif [ -f "${IMAGE_DIR}/kernel-runtime-raw.bin" ]; then
	KERNEL_ARG="/workspace/qemu_mps2_an505_images/kernel-runtime-raw.bin"
elif [ -f "${IMAGE_DIR}/flash-runtime.bin" ]; then
	KERNEL_ARG="/workspace/qemu_mps2_an505_images/flash-runtime.bin"
elif [ -f "${IMAGE_DIR}/kernel-runtime.bin" ]; then
	KERNEL_ARG="/workspace/qemu_mps2_an505_images/kernel-runtime.bin"
else
	echo "ERROR: no runnable kernel image found under ${IMAGE_DIR}"
	exit 1
fi

FLASH_LOADER_ARGS=()
if config_enabled CONFIG_APP_BINARY_SEPARATION && [ -f "${IMAGE_DIR}/flash-runtime.bin" ]; then
	FLASH_START=$(config_value CONFIG_FLASH_START_ADDR)
	FLASH_START=${FLASH_START%\"}
	FLASH_START=${FLASH_START#\"}
	FLASH_LOADER_ARGS=(
		-device
		"loader,file=/workspace/qemu_mps2_an505_images/flash-runtime.bin,addr=${FLASH_START},force-raw=on"
	)
fi

XIP_EXEC_LOADER_ARGS=()
if config_enabled CONFIG_XIP_ELF; then
	readarray -t XIP_EXEC_LOADER_ARGS < <(
		TOPDIR_ENV="${TOPDIR}" IMAGE_DIR_ENV="${IMAGE_DIR}" CONFIG_FILE_ENV="${CONFIG_FILE}" \
		python3 <<'PY'
import os
import struct
from pathlib import Path


def parse_config(path: Path):
    config = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        config[key] = value.strip().strip('"')
    return config


def userspace_text_field_offset(config):
    offset = 4
    if config.get("CONFIG_DISABLE_PTHREAD") != "y":
        offset += 4
    if config.get("CONFIG_DISABLE_SIGNALS") != "y":
        offset += 4
    return offset


def exec_base(image_path: Path, config, header_size: int):
    image_offset = 4 + header_size
    uspace_offset = image_offset + 4
    text_start_offset = uspace_offset + userspace_text_field_offset(config)
    data = image_path.read_bytes()
    text_start = struct.unpack_from("<I", data, text_start_offset)[0]
    return text_start - image_offset


config = parse_config(Path(os.environ["CONFIG_FILE_ENV"]))
image_dir = Path(os.environ["IMAGE_DIR_ENV"])
for name, header_size in (("common.trpk", 12), ("app1.trpk", 44)):
    image_path = image_dir / name
    if image_path.exists():
        print("-device")
        print(f"loader,file=/workspace/qemu_mps2_an505_images/{name},addr=0x{exec_base(image_path, config, header_size):08x},force-raw=on")
PY
	)
fi

docker run --rm ${DOCKER_TTY_FLAGS} \
	-v "${TOPDIR}:/workspace" \
	-w /workspace \
	"${RUNTIME_IMAGE}" \
	qemu-system-arm \
	-machine mps2-an505 \
	-cpu cortex-m33 \
	-nographic \
	-monitor none \
	-nic none \
	-kernel "${KERNEL_ARG}" \
	"${FLASH_LOADER_ARGS[@]}" \
	"${XIP_EXEC_LOADER_ARGS[@]}"
