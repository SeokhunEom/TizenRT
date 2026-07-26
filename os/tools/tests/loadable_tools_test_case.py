############################################################################
#
# Copyright 2026 Samsung Electronics All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
############################################################################

import os
import tempfile
import unittest
from pathlib import Path
from typing import Final

from loadable_tools_test_support import ToolSandbox


SOURCE_TOOLS_DIR: Final = Path(
    os.environ.get("TIZENRT_TOOL_TEST_ROOT", str(Path(__file__).resolve().parents[1]))
).resolve()


class LoadableToolTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.sandbox = ToolSandbox.copy_from(SOURCE_TOOLS_DIR, self.root / "sandbox")

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()
