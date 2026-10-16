"""Optional symbol lookup, requiring explicit trusted local build artifacts.

No command or path is obtained from the log. ELF symbolization alone starts a
subprocess and never uses a shell. Map lookup is approximate and offline.
"""

import bisect
import copy
import os
from pathlib import Path
import re
import subprocess

MAX_ADDRESSES = 512
MAX_ARTIFACT_BYTES = 256 * 1024 * 1024


def _artifact(path, magic=None):
    value = Path(path).expanduser()
    if not value.is_absolute():
        raise ValueError("artifact paths must be absolute")
    value = value.resolve(strict=True)
    if not value.is_file() or value.stat().st_size > MAX_ARTIFACT_BYTES:
        raise ValueError("artifact must be a regular file no larger than 256 MiB")
    if magic:
        with value.open("rb") as stream:
            if stream.read(len(magic)) != magic:
                raise ValueError("ELF artifact has no ELF magic")
    return value


def addresses(report):
    """Only code-address candidates, never arbitrary words from a stack dump."""
    candidates = []
    for event in report["events"]:
        data = event["data"]
        if event["kind"] == "callstack_frame":
            candidates.append((data["address"], event["line"], "callstack_frame"))
        if event["kind"] == "fault" and data.get("instruction"):
            candidates.append((data["instruction"], event["line"], "fault_instruction"))
        if event["kind"] == "registers":
            for name in ("PC", "LR", "ASSERT_PC"):
                if name in data["values"]:
                    candidates.append((data["values"][name], event["line"], name))
        if event["kind"] == "heap_caller":
            candidates.append((data["address"], event["line"], data["role"]))
        if event["kind"] == "workqueue_context":
            candidates.append((data["function_address"], event["line"], "workqueue_function"))
    unique = {}
    for value, line, role in candidates:
        if not re.fullmatch(r"0x[0-9a-fA-F]{1,16}", value):
            raise ValueError("invalid address in report")
        key = int(value, 16)
        unique.setdefault(key, {"address": value, "evidence_lines": [], "roles": []})
        if line not in unique[key]["evidence_lines"]:
            unique[key]["evidence_lines"].append(line)
        if role not in unique[key]["roles"]:
            unique[key]["roles"].append(role)
    if len(unique) > MAX_ADDRESSES:
        raise ValueError("more than 512 code addresses; narrow the report to an incident")
    return list(unique.values())


def symbolize(report, elf_path, addr2line_path, timeout=5):
    """Return a copy of report with matching-ELF lookups (explicit opt-in).

    The caller must supply trusted build artifacts/tools. The matching build
    identity is not established by this function. No installation is attempted.
    """
    if not isinstance(timeout, (int, float)) or not 0 < timeout <= 30:
        raise ValueError("timeout must be greater than 0 and at most 30 seconds")
    elf = _artifact(elf_path, b"\x7fELF")
    executable = Path(addr2line_path).expanduser()
    if not executable.is_absolute():
        raise ValueError("addr2line path must be absolute; PATH search is disabled")
    executable = executable.resolve(strict=True)
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError("addr2line must be an executable regular file")
    if not re.fullmatch(r"(?:[\w.+-]+-)?addr2line(?:-[\d.]+)?", executable.name):
        raise ValueError("explicit executable must be an addr2line tool")
    result = copy.deepcopy(report)
    entries = addresses(report)
    if entries:
        # Batch output is two lines per address without inline frames. Mask Thumb
        # bit only; do not guess an LR callsite adjustment or address relocation.
        command = [str(executable), "-f", "-C", "-e", str(elf)]
        command.extend("0x%x" % (int(e["address"], 16) & ~1) for e in entries)
        child = subprocess.run(command, shell=False, capture_output=True, text=True,
                               errors="replace", timeout=timeout, check=False,
                               env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"})
        if child.returncode:
            raise ValueError("addr2line failed with exit code %d: %s" % (child.returncode, child.stderr[:1000]))
        output = child.stdout.splitlines()
        if len(output) != len(entries) * 2:
            raise ValueError("unexpected addr2line output; expected function/location pairs")
        for index, entry in enumerate(entries):
            function, location = output[index * 2:index * 2 + 2]
            entry.update(function=function, location=location,
                         lookup_address="0x%x" % (int(entry["address"], 16) & ~1),
                         resolved=function != "??" and not location.startswith("??"))
    result["symbolization"] = {"method": "addr2line", "elf": str(elf), "tool": str(executable),
                               "entries": entries, "build_identity_verified": False,
                               "limitation": "Caller must verify matching ELF/build ID and load address. Thumb bit is cleared; LR is not adjusted."}
    return result


def symbolize_map(report, map_path):
    """Annotate nearest symbols from GNU linker maps or nm output conservatively."""
    path = _artifact(map_path)
    symbols = {}
    with path.open(encoding="utf-8", errors="replace") as stream:
        for line in stream:
            # nm: 10000000 T function; GNU ld: 0x10000000 function
            match = re.fullmatch(r"\s*(?:0x)?([\da-fA-F]{4,16})\s+(?:[tTwW]\s+)?([A-Za-z_$][\w.$:@<>~+-]*)\s*", line)
            if match:
                symbols[int(match[1], 16)] = match[2]
    ordered = sorted(symbols)
    entries = addresses(report)
    for entry in entries:
        address = int(entry["address"], 16) & ~1
        index = bisect.bisect_right(ordered, address) - 1
        if index >= 0:
            base = ordered[index]
            entry.update(symbol=symbols[base], symbol_address="0x%x" % base, offset=address - base,
                         confidence="nearest_preceding_symbol_without_size", resolved=False)
        else:
            entry.update(symbol=None, resolved=False)
    result = copy.deepcopy(report)
    result["symbolization"] = {"method": "map_nearest_symbol", "map": str(path), "entries": entries,
                               "build_identity_verified": False,
                               "limitation": "Nearest preceding map symbol is a candidate, not proof of code membership. Symbol sizes and matching build/load addresses are not verified."}
    return result
