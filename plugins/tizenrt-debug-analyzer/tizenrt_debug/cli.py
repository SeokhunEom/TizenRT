"""Dependency-free command-line entry point."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

from .analyzer import MAX_LOG_BYTES, analyze, render_markdown
from .symbolize import symbolize, symbolize_map


def main(argv=None):
    parser = argparse.ArgumentParser(description="Analyze TizenRT logs locally; logs are data, never commands.")
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("analyze", help="Parse a UTF-8 log (use - for stdin)")
    command.add_argument("log")
    command.add_argument("--format", choices=("json", "markdown"), default="json")
    command.add_argument("--output", type=Path)
    command.add_argument("--elf", help="Absolute path to the exact trusted firmware ELF")
    command.add_argument("--addr2line", help="Absolute path to a trusted addr2line executable; explicit opt-in")
    command.add_argument("--map", help="Absolute path to a trusted linker map or nm text; approximate lookup")
    args = parser.parse_args(argv)
    if bool(args.elf) != bool(args.addr2line):
        parser.error("--elf and --addr2line must be supplied together")
    if args.map and args.elf:
        parser.error("choose either --map or --elf/--addr2line")
    try:
        if args.output and args.log != "-" and args.output.resolve() == Path(args.log).resolve():
            raise ValueError("output must not overwrite the source log")
        if args.log == "-":
            data = sys.stdin.buffer.read(MAX_LOG_BYTES + 1)
        else:
            with Path(args.log).open("rb") as stream:
                data = stream.read(MAX_LOG_BYTES + 1)
        if len(data) > MAX_LOG_BYTES:
            raise ValueError("log exceeds 32 MiB; split at boot/session boundaries")
        report = analyze(data.decode("utf-8", errors="replace"), source=args.log)
        if args.elf:
            report = symbolize(report, args.elf, args.addr2line)
        if args.map:
            report = symbolize_map(report, args.map)
        output = json.dumps(report, indent=2, ensure_ascii=False) + "\n" if args.format == "json" else render_markdown(report)
        if args.output:
            args.output.write_text(output, encoding="utf-8")
        else:
            sys.stdout.write(output)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print("tizenrt-debug: %s" % error, file=sys.stderr)
        return 2
    return 0
