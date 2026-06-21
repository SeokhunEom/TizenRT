#!/usr/bin/env python3
#
# Copyright 2026 Samsung Electronics All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0
#

import argparse
import json
import re
import sys


PREFIX = "#TLOG"


def load_dictionary(path):
    with open(path) as f:
        data = json.load(f)
    entries = {}
    for entry in data.get("entries", []):
        domain = int(entry["domain"])
        log_id = int(entry["id"], 16)
        entries[(domain, log_id)] = entry
    return entries


def unescape(value):
    out = []
    idx = 0
    while idx < len(value):
        if (value[idx] == "%" and idx + 2 < len(value) and
                re.match(r"[0-9A-Fa-f]{2}", value[idx + 1:idx + 3])):
            out.append(chr(int(value[idx + 1:idx + 3], 16)))
            idx += 3
        else:
            out.append(value[idx])
            idx += 1
    return "".join(out)


def arg_types(argdesc):
    value = int(argdesc, 16)
    types = []
    for index in range(16):
        typ = (value >> (index * 4)) & 0x0f
        if typ == 0:
            break
        types.append(typ)
    return types


def convert_format(fmt):
    result = []
    pos = 0
    length = len(fmt)

    while pos < length:
        if fmt[pos] != "%":
            result.append(fmt[pos])
            pos += 1
            continue

        start = pos
        pos += 1
        if pos < length and fmt[pos] == "%":
            result.append("%%")
            pos += 1
            continue

        while pos < length and fmt[pos] in "#0- +'":
            pos += 1
        if pos < length and fmt[pos] == "*":
            pos += 1
        else:
            while pos < length and fmt[pos].isdigit():
                pos += 1
        if pos < length and fmt[pos] == ".":
            pos += 1
            if pos < length and fmt[pos] == "*":
                pos += 1
            else:
                while pos < length and fmt[pos].isdigit():
                    pos += 1

        if pos + 1 < length and fmt[pos:pos + 2] in ("hh", "ll"):
            pos += 2
        elif pos < length and fmt[pos] in "hljztL":
            pos += 1

        if pos >= length:
            result.append(fmt[start:])
            break

        conv = fmt[pos]
        pos += 1
        spec = fmt[start:pos]
        spec = re.sub(r"(hh|ll|[hljztL])(?=[diuoxXscp])", "", spec)
        if conv == "p":
            spec = spec[:-1] + "s"
        elif conv == "i":
            spec = spec[:-1] + "d"
        result.append(spec)

    return "".join(result)


def coerce_args(types, values):
    coerced = []
    for typ, value in zip(types, values):
        value = unescape(value)
        if typ in (1, 2, 3, 4, 5, 6, 10, 11, 12, 13, 14):
            coerced.append(int(value, 0))
        elif typ == 7:
            coerced.append(value)
        elif typ == 8:
            coerced.append(value)
        elif typ == 9:
            ivalue = int(value, 0)
            coerced.append(chr(ivalue) if 0 <= ivalue <= 255 else ivalue)
        else:
            coerced.append(value)
    return tuple(coerced)


def decode_line(entries, line):
    prefix_pos = line.find(PREFIX + "|")
    if prefix_pos < 0:
        return line

    line_prefix = line[:prefix_pos]
    frame = line[prefix_pos:]
    parts = frame.rstrip("\n").split("|")
    if len(parts) < 7:
        return line

    try:
        version = int(parts[1])
        domain = int(parts[2])
        log_id = int(parts[3], 16)
    except ValueError:
        return line

    if version != 1:
        return line

    entry = entries.get((domain, log_id))
    if not entry:
        return line

    types = arg_types(entry["argdesc"])
    values = parts[7:]
    if len(values) < len(types):
        return line

    try:
        fmt = convert_format(entry["format"])
        rendered = line_prefix + (fmt % coerce_args(types, values))
    except Exception:
        rendered = "%s%s %s" % (line_prefix, entry["format"],
                                " ".join(values))

    return rendered


def main(argv):
    parser = argparse.ArgumentParser(description="Decode TizenRT #TLOG frames")
    parser.add_argument("dictionary")
    parser.add_argument("input", nargs="?")
    args = parser.parse_args(argv)

    entries = load_dictionary(args.dictionary)
    stream = open(args.input) if args.input else sys.stdin
    try:
        for line in stream:
            sys.stdout.write(decode_line(entries, line))
    finally:
        if args.input:
            stream.close()

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
