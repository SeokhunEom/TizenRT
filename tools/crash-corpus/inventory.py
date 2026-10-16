#!/usr/bin/env python3
"""Reproducible lexical inventory of TizenRT diagnostic-output candidates.

This is source discovery, not a preprocessor, AST, or runtime coverage report.
It never executes firmware or treats a discovered call as parser support.
"""
from __future__ import annotations

import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOTS = ("os", "apps", "framework")
SUFFIXES = {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".s", ".S"}
EMITTER = re.compile(
    r"\b(?P<name>(?:[A-Za-z_]\w*)?dbg(?:_noarg)?|"
    r"(?:ll|low|v|f|d)?printf|syslog|lowsyslog|vsyslog|logm|"
    r"berr|bwarn|binfo|irqinfo|i2cerr|i2cwarn|i2cinfo|i2serr|i2swarn|i2sinfo|[A-Za-z_]\w*dumpbuffer|"
    r"puts|fputs|_alert|_err|_warn|_info|"
    r"(?:[A-Z_]*)LOG[EWIDV]|(?:[A-Z_]*)(?:PRINTF|PRINT|LOG|LOGE|LOGW|LOGI|LOGD))\s*\("
)
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
STRING = re.compile(r'"(?:\\[\s\S]|[^"\\])*"')
DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)")


def masked_source(source: str) -> tuple[str, str]:
    """Mask comments; additionally mask literals for detecting calls, retaining offsets."""
    comments, code = list(source), list(source)
    for match in TOKEN.finditer(source):
        is_comment = source[match.start():match.start() + 2] in ("//", "/*")
        for index in range(match.start(), match.end()):
            if source[index] != "\n":
                code[index] = " "
                if is_comment:
                    comments[index] = " "
    return "".join(comments), "".join(code)


def conditions_by_line(code: str) -> list[list[str]]:
    result, stack = [], []
    lines = code.splitlines()
    index = 0
    while index < len(lines):
        line, count = lines[index], 1
        if line.lstrip().startswith("#"):
            while line.rstrip().endswith("\\") and index + count < len(lines):
                line = line.rstrip()[:-1] + " " + lines[index + count]
                count += 1
        match = DIRECTIVE.match(line)
        if match:
            op, expr = match.groups()
            expr = expr.strip()
            if op in ("if", "ifdef", "ifndef"):
                condition = expr if op == "if" else f"{'!' if op == 'ifndef' else ''}defined({expr})"
                stack.append({"seen": [condition], "active": condition})
            elif op in ("else", "elif") and stack:
                frame = stack[-1]
                inverse = "!(" + " || ".join(f"({x})" for x in frame["seen"]) + ")"
                frame["active"] = inverse if op == "else" else f"{inverse} && ({expr})"
                if op == "elif":
                    frame["seen"].append(expr)
            elif op == "endif" and stack:
                stack.pop()
        result.extend([[frame["active"] for frame in stack] for _ in range(count)])
        index += count
    return result


def scan_file(root: Path, path: Path) -> tuple[list[dict], str]:
    raw = path.read_bytes()
    source = raw.decode("utf-8", errors="replace")
    comments, code = masked_source(source)
    conditions = conditions_by_line(code)
    directive_lines = set()
    continuing = False
    for number, physical_line in enumerate(code.splitlines(), 1):
        is_directive = continuing or physical_line.lstrip().startswith("#")
        if is_directive:
            directive_lines.add(number)
        continuing = is_directive and physical_line.rstrip().endswith("\\")
    entries = []
    for match in EMITTER.finditer(code):
        line = source.count("\n", 0, match.start()) + 1
        # Macro definitions/routes are not output callsites.
        if line in directive_lines:
            continue
        depth, end = 1, match.end()
        while end < len(code) and depth:
            depth += (code[end] == "(") - (code[end] == ")")
            end += 1
        args = comments[match.end():end - 1]
        strings = [m.group()[1:-1] for m in STRING.finditer(args)]
        active = conditions[line - 1] if line <= len(conditions) else []
        entries.append({
            "path": path.relative_to(root).as_posix(), "line": line,
            "emitter": match.group("name"),
            "literal_fragments": strings,
            "format_resolution": "literal_candidates" if strings else "dynamic_or_macro",
            "config_conditions": active,
            "config_symbols": sorted(set(re.findall(r"\bCONFIG_\w+", " ".join(active)))),
        })
    return entries, hashlib.sha256(raw).hexdigest()


def inventory(root: Path, roots: list[str]) -> dict:
    # rg enumerates real available source only, respecting repository ignore rules.
    run = subprocess.run(["rg", "--files", "--hidden", *roots], cwd=root, check=True, capture_output=True, text=True)
    files = sorted(p for p in run.stdout.splitlines() if Path(p).suffix in SUFFIXES)
    entries, digests, skipped = [], {}, []
    for relative in files:
        path = root / relative
        if path.is_symlink():
            skipped.append(relative)
            continue
        found, digest = scan_file(root, path)
        entries.extend(found)
        digests[relative] = digest
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, check=True, capture_output=True, text=True).stdout.strip()
    by_subsystem = Counter("/".join(e["path"].split("/")[:2]) for e in entries)
    return {
        "schema_version": 1, "source_revision": revision,
        "scope": {"roots": roots, "suffixes": sorted(SUFFIXES), "enumerator": "rg --files --hidden; respects ignore rules", "emitter_regex": EMITTER.pattern},
        "limitations": [
            "Lexical source candidates; no claim of compiled, enabled, reached, collected or semantically parsed output.",
            "Conditions are lexical enclosing preprocessor directives, not evaluated Kconfig/Makefile dependencies or transitive macro routes.",
            "Literal fragments retain C escapes; macro formats, concatenated identifiers, nested-call literals and function declarations require source review.",
            "Known output API names and dbg/log naming conventions only; arbitrary vendor wrappers, binary libraries, inactive submodules and other file types can be absent.",
            "snprintf/sprintf buffer construction and low-level putchar/write are excluded; procfs format definitions require the curated catalog.",
            "Source hashes capture current working files, which can differ from source_revision; generated ignored files and symlinks are not scanned.",
        ],
        "summary": {"source_files_scanned": len(digests), "files_with_candidates": len(set(e["path"] for e in entries)), "candidate_calls": len(entries), "dynamic_or_macro_calls": sum(e["format_resolution"] == "dynamic_or_macro" for e in entries), "by_subsystem": dict(sorted(by_subsystem.items())), "by_emitter": dict(sorted(Counter(e["emitter"] for e in entries).items())), "symlinks_skipped": skipped},
        "source_sha256": digests, "entries": entries,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--roots", nargs="+", default=list(ROOTS))
    parser.add_argument("--output", type=Path, required=True, help="JSON, or reproducible gzip JSON when suffix is .gz")
    parser.add_argument("--summary", type=Path, help="Optional small report without entries and per-file hashes")
    args = parser.parse_args()
    result = inventory(args.root.resolve(), args.roots)
    raw = (json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n").encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(gzip.compress(raw, mtime=0) if args.output.suffix == ".gz" else raw)
    if args.summary:
        summary = {key: value for key, value in result.items() if key not in ("source_sha256", "entries")}
        summary["inventory_file"] = args.output.name
        summary["inventory_sha256"] = hashlib.sha256(args.output.read_bytes()).hexdigest()
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"source_files_scanned": result["summary"]["source_files_scanned"], "candidate_calls": len(result["entries"]), "output": str(args.output)}))


if __name__ == "__main__":
    main()
