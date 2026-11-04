import tempfile
import unittest
from pathlib import Path

from matrix import BOARDS, discover


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        for board in BOARDS:
            self.config(board, "hello", "# no explicit version\n")

    def config(self, board, name, content):
        path = self.root / board / name / "defconfig"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def test_new_defconfigs_are_included_and_unrelated_boards_are_ignored(self):
        self.config("rtl8730e", "new_config", 'CONFIG_DOCKER_VERSION="2.0.0"\n')
        self.config("bk7239n", "hello", 'CONFIG_DOCKER_VERSION="2.0.1"\r\n')
        self.config("other_board", "hello", "")
        entries = discover(self.root)["include"]
        self.assertEqual(len(entries), 5)
        images = {entry["config"]: entry["image"] for entry in entries}
        self.assertEqual(images["rtl8730e/new_config"], "tizenrt/tizenrt:2.0.0")
        self.assertEqual(images["bk7239n/hello"], "tizenrt/tizenrt:2.0.1")
        self.assertEqual(images["qemu/hello"], "tizenrt/tizenrt:1.5.8")

    def test_missing_board_fails_instead_of_silently_reducing_coverage(self):
        (self.root / "qemu/hello/defconfig").unlink()
        with self.assertRaisesRegex(ValueError, "No defconfigs.*qemu"):
            discover(self.root)

    def test_invalid_or_duplicate_image_version_fails(self):
        for content in ('CONFIG_DOCKER_VERSION="$(touch nope)"\n',
                        'CONFIG_DOCKER_VERSION=""\n',
                        'CONFIG_DOCKER_VERSION="2.0.0"\n' * 2):
            with self.subTest(content=content):
                self.config("rtl8730e", "hello", content)
                with self.assertRaises(ValueError):
                    discover(self.root)

    def test_unsupported_nested_defconfig_fails_instead_of_being_omitted(self):
        self.config("qemu", "nested/hello", "")
        with self.assertRaisesRegex(ValueError, "Unsupported configuration path"):
            discover(self.root)


if __name__ == "__main__":
    unittest.main()
