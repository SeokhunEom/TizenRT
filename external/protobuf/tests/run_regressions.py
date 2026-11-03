#!/usr/bin/env python3
"""Exercise the production DataPiece API with undefined-behavior sanitizers.

Link only the numeric conversion dependencies; unused protocol/descriptor code
is discarded by the host linker. Requires a C++11 compiler with UBSan support.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

here = pathlib.Path(__file__).resolve().parent
source = here.parent / 'src'
protobuf = source / 'google/protobuf'
dependencies = [
    'util/internal/datapiece.cc',
    'util/internal/utility.cc',
    'stubs/status.cc',
    'stubs/statusor.cc',
    'stubs/strutil.cc',
    'stubs/stringpiece.cc',
    'stubs/common.cc',
    'stubs/int128.cc',
    'stubs/stringprintf.cc',
    'stubs/once.cc',
    'io/strtod.cc',
]
with tempfile.TemporaryDirectory() as temporary:
    binary = pathlib.Path(temporary) / 'datapiece-conversion'
    subprocess.run([
        os.environ.get('CXX', 'c++'), '-std=c++11', '-O1', '-DHAVE_PTHREAD', '-pthread',
        '-ffunction-sections', '-fdata-sections',
        '-fsanitize=undefined,float-cast-overflow', '-fno-sanitize-recover=all',
        '-include', str(here / 'host_math_compat.h'),
        '-I', str(source), str(here / 'test_datapiece_conversion.cc'),
        *[str(protobuf / path) for path in dependencies],
        '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections',
        '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
print('DataPiece conversion boundary regression tests passed')
