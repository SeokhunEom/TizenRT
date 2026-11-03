#!/usr/bin/env python3
"""Native tests of calendar parsing and the Mbed TLS 3 key-export adapter.

The PRF is a test double: these tests verify the TLS 1.2 seed, metadata,
callback data, failures and zeroization, not Mbed TLS cryptographic primitives.
"""
import pathlib
import subprocess
import tempfile

here = pathlib.Path(__file__).resolve().parent
root = here.parents[2]
iotivity = here.parent / 'iotivity_1.2-rel'
with tempfile.TemporaryDirectory() as temporary:
    temp = pathlib.Path(temporary)
    (temp / 'tinyara').mkdir()
    (temp / 'tinyara/config.h').write_text('')
    (temp / 'mbedtls_test_config.h').write_text('')
    (temp / 'iotivity_config.h').write_text('#define HAVE_STRPTIME 1\n')
    common = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(temp)]
    export_binary = temp / 'key-export'
    subprocess.run(common + [
        '-DMBEDTLS_CONFIG_FILE="mbedtls_test_config.h"',
        '-I', str(root / 'external/include'),
        '-I', str(iotivity / 'resource/csdk/connectivity/src/adapter_util'),
        str(here / 'test_key_export.c'), '-o', str(export_binary),
    ], check=True)
    subprocess.run([str(export_binary)], check=True)
    calendar_binary = temp / 'calendar'
    subprocess.run(common + [
        '-I', str(iotivity / 'resource/csdk/security/include'),
        '-I', str(iotivity / 'resource/c_common/oic_string/include'),
        str(here / 'test_calendar.c'),
        str(iotivity / 'resource/csdk/security/src/iotvticalendar.c'),
        '-o', str(calendar_binary),
    ], check=True)
    subprocess.run([str(calendar_binary)], check=True)
print('calendar and key-export regression tests passed')
