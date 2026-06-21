#!/usr/bin/env python3
#
# Copyright 2026 Samsung Electronics All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0
#

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys


LOGDICT_SITE_MAGIC = 0x47444C54
SITE_RECORD_BASE_SIZE = 24

ARG_END = 0
ARG_INT = 1
ARG_UINT = 2
ARG_LONG = 3
ARG_ULONG = 4
ARG_LONGLONG = 5
ARG_ULONGLONG = 6
ARG_PTR = 7
ARG_STRING = 8
ARG_CHAR = 9
ARG_SIZE = 10
ARG_SSIZE = 11
ARG_INTMAX = 12
ARG_UINTMAX = 13
ARG_STRPREC = 14

UNSUPPORTED = object()


class ElfError(Exception):
    pass


class ElfFile:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = bytearray(f.read())

        if self.data[:4] != b"\x7fELF":
            raise ElfError("%s is not an ELF file" % path)

        self.elf_class = self.data[4]
        self.elf_data = self.data[5]
        if self.elf_class not in (1, 2):
            raise ElfError("unsupported ELF class %d" % self.elf_class)
        if self.elf_data not in (1, 2):
            raise ElfError("unsupported ELF data encoding %d" % self.elf_data)

        self.endian = "<" if self.elf_data == 1 else ">"
        self.elf_type, = self._unpack_from("H", 16)
        self.sections = self._read_sections()

    def _unpack_from(self, fmt, offset):
        return struct.unpack_from(self.endian + fmt, self.data, offset)

    def _read_sections(self):
        if self.elf_class == 1:
            e_shoff, = self._unpack_from("I", 32)
            e_shentsize, = self._unpack_from("H", 46)
            e_shnum, = self._unpack_from("H", 48)
            e_shstrndx, = self._unpack_from("H", 50)
            sh_fmt = "IIIIIIIIII"
        else:
            e_shoff, = self._unpack_from("Q", 40)
            e_shentsize, = self._unpack_from("H", 58)
            e_shnum, = self._unpack_from("H", 60)
            e_shstrndx, = self._unpack_from("H", 62)
            sh_fmt = "IIQQQQIIQQ"

        if e_shoff == 0 or e_shnum == 0:
            raise ElfError("ELF has no section header table")

        raw = []
        for idx in range(e_shnum):
            off = e_shoff + idx * e_shentsize
            vals = self._unpack_from(sh_fmt, off)
            if self.elf_class == 1:
                name, stype, flags, addr, offset, size, link, info, addralign, entsize = vals
            else:
                name, stype, flags, addr, offset, size, link, info, addralign, entsize = vals
            raw.append({
                "index": idx,
                "name_off": name,
                "type": stype,
                "flags": flags,
                "addr": addr,
                "offset": offset,
                "size": size,
                "link": link,
                "info": info,
                "addralign": addralign,
                "entsize": entsize,
            })

        if e_shstrndx >= len(raw):
            raise ElfError("invalid section string table index")

        shstr = self.section_bytes(raw[e_shstrndx])
        for sec in raw:
            sec["name"] = self._read_cstr(shstr, sec["name_off"])

        return raw

    def _read_cstr(self, data, offset):
        end = data.find(b"\0", offset)
        if end < 0:
            end = len(data)
        return data[offset:end].decode("utf-8", "replace")

    def section(self, name):
        for sec in self.sections:
            if sec["name"] == name:
                return sec
        return None

    def section_bytes(self, sec):
        start = sec["offset"]
        end = start + sec["size"]
        return bytes(self.data[start:end])

    def patch_site_record(self, sec, index, record_size, log_id, argdesc):
        offset = sec["offset"] + index * record_size
        struct.pack_into(self.endian + "I", self.data, offset + 4, log_id)
        struct.pack_into(self.endian + "I", self.data, offset + 12,
                         argdesc & 0xffffffff)
        struct.pack_into(self.endian + "I", self.data, offset + 16,
                         (argdesc >> 32) & 0xffffffff)

    def write(self, path):
        with open(path, "wb") as f:
            f.write(self.data)


def fnv1a32(text):
    value = 0x811C9DC5
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 0x01000193) & 0xffffffff
    return value


def split_meta(data):
    records = []
    offset = 0
    for item in data.split(b"\0"):
        item_len = len(item) + 1
        if not item:
            offset += item_len
            continue
        text = item.decode("utf-8", "replace")
        parts = text.split("\x1f", 2)
        if len(parts) != 3:
            raise ElfError("invalid .logdict_meta record: %r" % text)
        file_name, line, fmt = parts
        records.append({
            "offset": offset,
            "file": file_name,
            "line": int(line),
            "format": fmt,
        })
        offset += item_len
    return records


def parse_sites(elf, sec, count, record_size):
    data = elf.section_bytes(sec)
    if len(data) != count * record_size:
        raise ElfError(".logdict_site size mismatch")
    if record_size < SITE_RECORD_BASE_SIZE:
        raise ElfError(".logdict_site record is smaller than %d bytes" %
                       SITE_RECORD_BASE_SIZE)

    sites = []
    for index in range(count):
        off = index * record_size
        magic, log_id, domain, priority, flags, arglo, arghi, line = \
            struct.unpack_from(elf.endian + "IIHBBIII", data, off)
        if magic != LOGDICT_SITE_MAGIC:
            raise ElfError("invalid logdict site magic at record %d" % index)
        sites.append({
            "index": index,
            "id": log_id,
            "domain": domain,
            "priority": priority,
            "flags": flags,
            "argdesc": arglo | (arghi << 32),
            "line": line,
            "meta_addr": read_meta_addr(elf, data, off, record_size),
        })
    return sites


def read_meta_addr(elf, data, offset, record_size):
    ptr_size = 4 if elf.elf_class == 1 else 8
    if record_size < SITE_RECORD_BASE_SIZE + ptr_size:
        return None
    fmt = "I" if ptr_size == 4 else "Q"
    value, = struct.unpack_from(elf.endian + fmt, data,
                                offset + SITE_RECORD_BASE_SIZE)
    return value


def match_metas(elf, sites, metas, meta_sec):
    meta_by_addr = {
        meta_sec["addr"] + meta["offset"]: meta for meta in metas
    }
    matched = []
    used = set()
    can_use_addr = elf.elf_type != 1 and all(
        site["meta_addr"] in meta_by_addr for site in sites)

    if can_use_addr:
        for site in sites:
            meta = meta_by_addr[site["meta_addr"]]
            if site["meta_addr"] in used:
                raise ElfError("duplicate logdict meta reference")
            used.add(site["meta_addr"])
            matched.append((site, meta))
        return matched

    matched = list(zip(sites, metas))
    for site, meta in matched:
        if site["line"] != meta["line"]:
            raise ElfError("site/meta order mismatch at record %d: %d != %d" %
                           (site["index"], site["line"], meta["line"]))
    return matched


def skip_number(fmt, pos):
    while pos < len(fmt) and fmt[pos].isdigit():
        pos += 1
    return pos


def parse_format(fmt):
    args = []
    pos = 0
    length = len(fmt)

    while pos < length:
        if fmt[pos] != "%":
            pos += 1
            continue

        pos += 1
        if pos < length and fmt[pos] == "%":
            pos += 1
            continue

        while pos < length and fmt[pos] in "#0- +'":
            pos += 1

        if pos < length and fmt[pos] == "*":
            args.append(ARG_INT)
            pos += 1
        else:
            pos = skip_number(fmt, pos)

        precision_star = False
        if pos < length and fmt[pos] == ".":
            pos += 1
            if pos < length and fmt[pos] == "*":
                precision_star = True
                pos += 1
            else:
                pos = skip_number(fmt, pos)

        modifier = ""
        if pos + 1 < length and fmt[pos:pos + 2] in ("hh", "ll"):
            modifier = fmt[pos:pos + 2]
            pos += 2
        elif pos < length and fmt[pos] in "hljztL":
            modifier = fmt[pos]
            pos += 1

        if pos >= length:
            return UNSUPPORTED

        conv = fmt[pos]
        pos += 1

        if precision_star:
            args.append(ARG_STRPREC if conv == "s" else ARG_INT)

        if conv in "di":
            if modifier == "j":
                args.append(ARG_INTMAX)
            elif modifier == "ll":
                args.append(ARG_LONGLONG)
            elif modifier in ("l", "t"):
                args.append(ARG_LONG)
            elif modifier == "z":
                args.append(ARG_SSIZE)
            else:
                args.append(ARG_INT)
        elif conv in "uoxX":
            if modifier == "j":
                args.append(ARG_UINTMAX)
            elif modifier == "ll":
                args.append(ARG_ULONGLONG)
            elif modifier in ("l", "t"):
                args.append(ARG_ULONG)
            elif modifier == "z":
                args.append(ARG_SIZE)
            else:
                args.append(ARG_UINT)
        elif conv == "p":
            args.append(ARG_PTR)
        elif conv == "s":
            args.append(ARG_STRING)
        elif conv == "c":
            args.append(ARG_CHAR)
        else:
            return UNSUPPORTED

        if len(args) > 16:
            return UNSUPPORTED

    argdesc = 0
    for index, arg in enumerate(args):
        argdesc |= arg << (index * 4)
    return argdesc


def git_revision(root):
    try:
        return subprocess.check_output(
            ["git", "-C", root, "rev-parse", "--short=12", "HEAD"],
            stderr=subprocess.DEVNULL).decode("ascii").strip()
    except Exception:
        return None


def strip_meta(objcopy, src, dst):
    subprocess.check_call([
        objcopy,
        "--remove-section", ".logdict_meta",
        src,
        dst,
    ])


def finalize(args):
    elf = ElfFile(args.elf)
    site_sec = elf.section(".logdict_site")
    meta_sec = elf.section(".logdict_meta")

    outdir = os.path.dirname(args.out)
    if outdir:
        os.makedirs(outdir, exist_ok=True)

    if site_sec is None and meta_sec is None:
        with open(args.out, "w") as f:
            json.dump({
                "version": 1,
                "entries": [],
            }, f, indent=2, sort_keys=True)
        return 0

    if site_sec is not None and meta_sec is None:
        if os.path.exists(args.out):
            return 0
        raise ElfError(".logdict_site exists without .logdict_meta; "
                       "dictionary output is missing")

    if site_sec is None or meta_sec is None:
        raise ElfError(".logdict_site and .logdict_meta must both exist")

    metas = split_meta(elf.section_bytes(meta_sec))
    if len(metas) == 0:
        if site_sec["size"] != 0:
            raise ElfError(".logdict_site exists without meta records")
        sites = []
        record_size = SITE_RECORD_BASE_SIZE
    else:
        if site_sec["size"] % len(metas) != 0:
            raise ElfError("site/meta count mismatch")
        record_size = site_sec["size"] // len(metas)
        sites = parse_sites(elf, site_sec, len(metas), record_size)

    entries = []
    seen = {}

    for site, meta in match_metas(elf, sites, metas, meta_sec):
        key = "%u:%s:%u:%s" % (
            site["domain"], meta["file"], meta["line"], meta["format"])
        log_id = fnv1a32(key)
        argdesc = parse_format(meta["format"])
        if argdesc is UNSUPPORTED:
            raise ElfError("unsupported format at %s:%d: %s" %
                           (meta["file"], meta["line"], meta["format"]))
        collision = seen.get((site["domain"], log_id))
        if collision and collision != key:
            raise ElfError("log id collision for 0x%08x" % log_id)
        seen[(site["domain"], log_id)] = key

        elf.patch_site_record(site_sec, site["index"], record_size, log_id,
                              argdesc)
        entries.append({
            "domain": site["domain"],
            "id": "0x%08x" % log_id,
            "priority": site["priority"],
            "flags": site["flags"],
            "argdesc": "0x%016x" % argdesc,
            "file": meta["file"],
            "line": meta["line"],
            "format": meta["format"],
        })

    root = os.path.abspath(os.path.join(os.path.dirname(args.elf), "..", "..",
                                        ".."))
    dictionary = {
        "version": 1,
        "frame": "text",
        "prefix": "#TLOG",
        "git": git_revision(root),
        "entries": entries,
    }

    with open(args.out, "w") as f:
        json.dump(dictionary, f, indent=2, sort_keys=True)
        f.write("\n")

    patched = args.elf + ".logdict.patched"
    stripped = args.elf + ".logdict.stripped"
    elf.write(patched)
    strip_meta(args.objcopy, patched, stripped)
    shutil.copymode(args.elf, stripped)
    os.replace(stripped, args.elf)
    try:
        os.unlink(patched)
    except OSError:
        pass

    return 0


def main(argv):
    parser = argparse.ArgumentParser(description="TizenRT log dictionary tool")
    sub = parser.add_subparsers(dest="command")
    sub.required = True

    finalize_parser = sub.add_parser("finalize")
    finalize_parser.add_argument("--elf", required=True)
    finalize_parser.add_argument("--objcopy", required=True)
    finalize_parser.add_argument("--out", required=True)
    finalize_parser.set_defaults(func=finalize)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (ElfError, subprocess.CalledProcessError) as exc:
        print("logdict: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
