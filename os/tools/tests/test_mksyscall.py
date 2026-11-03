#!/usr/bin/env python3
"""Compile and execute generated stubs with optional syscalls on and off."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class SyscallStubTest(unittest.TestCase):
    def test_optional_void_integer_and_pointer_returns(self):
        tools = Path(__file__).resolve().parents[1]
        cc = os.environ.get("CC", "cc")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            generator = root / "mksyscall"
            source = Path(os.environ.get("MKSYSCALL_SOURCE", tools / "mksyscall.c"))
            subprocess.run([cc, "-I", str(tools), str(source),
                            str(tools / "csvparser.c"), "-o", str(generator)], check=True)
            (root / "tinyara").mkdir()
            (root / "tinyara/config.h").write_text("")
            (root / "fixture.h").write_text("""
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#define set_errno(value) (errno = (value))
void optional_void(int value);
int optional_int(int value);
void *optional_pointer(void *value);
""")
            (root / "fixture.csv").write_text('''
"optional_void", "fixture.h", "WITH_FEATURE", "void", "int"
"optional_int", "fixture.h", "WITH_FEATURE", "int", "int"
"optional_pointer", "fixture.h", "WITH_FEATURE", "void *", "void *"
''')
            subprocess.run([str(generator), "-s", "fixture.csv"], cwd=root, check=True)
            (root / "check.c").write_text("""
#include <assert.h>
#include "STUB_optional_void.c"
#include "STUB_optional_int.c"
#include "STUB_optional_pointer.c"
static int observed;
void optional_void(int value) { observed = value; }
int optional_int(int value) { return value + 1; }
void *optional_pointer(void *value) { return value; }
int main(void) {
    int object;
    errno = 0;
    assert(STUB_optional_void(0, 7) == 0);
#if WITH_FEATURE
    assert(observed == 7);
    assert(errno == 0);
    assert(STUB_optional_int(0, 7) == 8);
    assert(STUB_optional_pointer(0, (uintptr_t)&object) == (uintptr_t)&object);
#else
    assert(observed == 0);
    assert(errno == ENOSYS);
    errno = 0;
    assert(STUB_optional_int(0, 7) == (uintptr_t)-1);
    assert(errno == ENOSYS);
    errno = 0;
    assert(STUB_optional_pointer(0, (uintptr_t)&object) == 0);
    assert(errno == ENOSYS);
#endif
    return 0;
}
""")
            for enabled in (0, 1):
                with self.subTest(enabled=enabled):
                    executable = root / f"check-{enabled}"
                    subprocess.run([cc, "-std=c99", "-Wall", "-Werror",
                                    f"-DWITH_FEATURE={enabled}", "-I", str(root),
                                    str(root / "check.c"), "-o", str(executable)], check=True)
                    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
