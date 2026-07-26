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

import struct
import sys

import config_util as util


BINARY_NAME_FIELD_SIZE = 16
RESOURCE_HEADER_TOTAL_SIZE = 4096
RESOURCE_HEADER = struct.Struct("<HII")


class BinaryNameError(ValueError):
    def __init__(self, binary_name, reason):
        self.binary_name = binary_name
        self.reason = reason
        super(BinaryNameError, self).__init__(reason)


def encode_binary_name(binary_name):
    if not binary_name:
        raise BinaryNameError(binary_name, "Binary name must not be empty")
    try:
        encoded_name = binary_name.encode("ascii")
    except UnicodeEncodeError:
        raise BinaryNameError(binary_name, "Binary name must contain only ASCII characters")
    if len(encoded_name) >= BINARY_NAME_FIELD_SIZE:
        raise BinaryNameError(binary_name, "Binary name must fit with a NUL terminator")
    return encoded_name.ljust(BINARY_NAME_FIELD_SIZE, b"\0")


def make_resource_binary_header(file_path, config_path):
    header_size = RESOURCE_HEADER.size
    remain_size = RESOURCE_HEADER_TOTAL_SIZE - header_size - struct.calcsize("<I")
    bin_ver = util.get_value_from_file(
        config_path,
        "CONFIG_RESOURCE_BINARY_VERSION=",
    ).replace('"', "").replace("\n", "")
    if bin_ver == "None":
        print("Error : Not Found config for resource binary version, CONFIG_RESOURCE_BINARY_VERSION")
        sys.exit(1)
    parsed_bin_ver = int(bin_ver)
    if parsed_bin_ver < 101 or parsed_bin_ver > 991231:
        print("Error : Invalid Resource Binary Version, ", parsed_bin_ver, ".")
        print("        Please check CONFIG_RESOURCE_BINARY_VERSION with 'YYMMDD' format in (101, 991231)")
        sys.exit(1)

    with open(file_path, "rb") as input_file:
        data = input_file.read()
        file_size = input_file.tell()

    with open(file_path, "wb") as output_file:
        output_file.write(RESOURCE_HEADER.pack(header_size, parsed_bin_ver, file_size))
        output_file.write(b"\xff" * remain_size)
        output_file.write(data)
