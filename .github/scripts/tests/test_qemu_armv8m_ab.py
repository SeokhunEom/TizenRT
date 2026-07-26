from __future__ import annotations

import importlib.util
import shutil
import struct
import tempfile
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / ".github/scripts/qemu_armv8m_ab.py"


def load_module():
    spec = importlib.util.spec_from_file_location("qemu_armv8m_ab_test", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def package(payload: bytes) -> bytes:
    data = bytearray(16 + len(payload))
    struct.pack_into("<H", data, 4, 12)
    struct.pack_into("<I", data, 10, len(payload))
    data[16:] = payload
    return bytes(data)


class QemuArmv8mAbTest(unittest.TestCase):
    def setUp(self) -> None:
        self.ab = load_module()
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def prepare_root(self, config: str) -> Path:
        target_config = self.root / "build/configs/qemu-armv8m" / config / "defconfig"
        target_config.parent.mkdir(parents=True)
        shutil.copy2(ROOT / "build/configs/qemu-armv8m" / config / "defconfig", target_config)
        bin_dir = self.root / "build/output/bin"
        bin_dir.mkdir(parents=True)
        (bin_dir / "tinyara.bin").write_bytes(package(b"kernel-a"))
        for name in ("common", "app1", "app2"):
            if name in ("app2",) and config == "xip_all":
                continue
            (bin_dir / name).write_bytes(package((name + "-a").encode()))
        return self.root

    def test_loadable_state_stages_two_apps_and_flips_kernel_slot(self) -> None:
        root = self.prepare_root("loadable_all")
        state = root / "state.bin"

        self.ab.stage_state(root, "loadable_all", state)
        self.assertEqual(self.ab.RAM_SIZE, state.stat().st_size)
        self.assertEqual(0, self.ab.active_slot(state, root, "loadable_all"))
        self.assertEqual(b"kernel-a", self.ab.extract_active_kernel(root, "loadable_all", state).read_bytes())

        self.ab.flip_active_slot(state, root, "loadable_all")
        self.assertEqual(1, self.ab.active_slot(state, root, "loadable_all"))
        self.assertEqual(b"kernel-a", self.ab.extract_active_kernel(root, "loadable_all", state).read_bytes())

        layout = self.ab.partition_layout(root, "loadable_all")
        image = state.read_bytes()
        bp_address = layout["bootparam"][0][0] - self.ab.RAM_BASE
        newest = image[bp_address + self.ab.BOOTPARAM_SLOT_SIZE:bp_address + self.ab.BOOTPARAM_SIZE]
        self.assertEqual(2, struct.unpack_from("<I", newest, self.ab.BP_VERSION_OFFSET)[0])
        self.assertEqual(1, newest[self.ab.BP_ACTIVE_IDX_OFFSET])
        app_count = newest[self.ab.BP_APP_COUNT_OFFSET]
        for app_index in range(app_count):
            useidx = self.ab.BP_APP_DATA_OFFSET + app_index * self.ab.BP_APP_DATA_STRIDE + self.ab.BP_APP_USEIDX_OFFSET
            self.assertEqual(1, newest[useidx])

    def test_xip_state_uses_alternate_common_and_app_packages(self) -> None:
        root = self.prepare_root("xip_all")
        bin_dir = root / "build/output/bin"
        (bin_dir / "common_1").write_bytes(package(b"common-b"))
        (bin_dir / "app1_1").write_bytes(package(b"app1-b"))
        state = root / "state.bin"

        self.ab.stage_state(root, "xip_all", state)
        layout = self.ab.partition_layout(root, "xip_all")
        image = state.read_bytes()
        common_b_address, _ = layout["common"][1]
        app1_b_address, _ = layout["app1"][1]
        self.assertEqual(0x80E80000, common_b_address)
        self.assertEqual(0x80F40000, app1_b_address)
        self.assertEqual(b"common-b", image[common_b_address - self.ab.RAM_BASE + 16:common_b_address - self.ab.RAM_BASE + 24])
        self.assertEqual(b"app1-b", image[app1_b_address - self.ab.RAM_BASE + 16:app1_b_address - self.ab.RAM_BASE + 22])

    def test_semantically_invalid_newer_bootparam_is_ignored(self) -> None:
        root = self.prepare_root("loadable_all")
        state = root / "state.bin"
        self.ab.stage_state(root, "loadable_all", state)

        layout = self.ab.partition_layout(root, "loadable_all")
        image = bytearray(state.read_bytes())
        bp_address = layout["bootparam"][0][0] - self.ab.RAM_BASE
        candidate_start = bp_address + self.ab.BOOTPARAM_SLOT_SIZE
        candidate = bytearray(image[bp_address:candidate_start])
        struct.pack_into("<I", candidate, self.ab.BP_VERSION_OFFSET, 99)
        struct.pack_into("<I", candidate, self.ab.BP_FORMAT_OFFSET, 99)
        struct.pack_into("<I", candidate, 0, zlib.crc32(candidate[4:]) & 0xffffffff)
        image[candidate_start:candidate_start + self.ab.BOOTPARAM_SLOT_SIZE] = candidate
        state.write_bytes(image)

        self.assertEqual(0, self.ab.active_slot(state, root, "loadable_all"))

    def test_state_supports_package_overrides_and_omitted_common(self) -> None:
        root = self.prepare_root("xip_all")
        bin_dir = root / "build/output/bin"
        custom_common = root / "corrupt-common"
        custom_common.write_bytes(package(b"common-corrupt"))
        custom_app = root / "corrupt-app1"
        custom_app.write_bytes(package(b"app1-corrupt"))

        override_state = root / "override.state"
        self.ab.stage_state(root, "xip_all", override_state, common_path=custom_common, app1_path=custom_app)
        layout = self.ab.partition_layout(root, "xip_all")
        image = override_state.read_bytes()
        common_address, _ = layout["common"][0]
        app1_address, _ = layout["app1"][0]
        self.assertEqual(b"common-corrupt", image[common_address - self.ab.RAM_BASE + 16:common_address - self.ab.RAM_BASE + 30])
        self.assertEqual(b"app1-corrupt", image[app1_address - self.ab.RAM_BASE + 16:app1_address - self.ab.RAM_BASE + 28])

        omitted_state = root / "omitted.state"
        self.ab.stage_state(root, "xip_all", omitted_state, omit_common=True)
        image = omitted_state.read_bytes()
        self.assertEqual(b"\xff" * 16, image[common_address - self.ab.RAM_BASE:common_address - self.ab.RAM_BASE + 16])

    def test_qemu_command_maps_state_image_as_persistent_ram(self) -> None:
        state = self.root / "state.bin"
        kernel = self.root / "kernel.bin"
        command = self.ab.qemu_command(state, kernel)

        self.assertIn("-object", command)
        self.assertIn(f"mem-path={state}", " ".join(command))
        self.assertIn("-no-reboot", command)
        self.assertIn(
            f"loader,file={kernel},addr=0x10000000,force-raw=on",
            command,
        )
        self.assertNotIn("-kernel", command)

    def test_generated_config_overrides_defconfig_for_matching_recipe(self) -> None:
        root = self.prepare_root("loadable_all")
        generated = root / "os/.config"
        generated.parent.mkdir(parents=True)
        source = (root / "build/configs/qemu-armv8m/loadable_all/defconfig").read_text(encoding="utf-8")
        generated.write_text(source.replace("CONFIG_FLASH_SIZE=4194304", "CONFIG_FLASH_SIZE=5242880", 1), encoding="utf-8")

        values = self.ab.read_config(root, "loadable_all")

        self.assertEqual("5242880", values["CONFIG_FLASH_SIZE"])


if __name__ == "__main__":
    unittest.main()
