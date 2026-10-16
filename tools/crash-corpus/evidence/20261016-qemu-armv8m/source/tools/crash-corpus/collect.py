#!/usr/bin/env python3
"""Collect real serial output from a fresh local QEMU guest per diagnostic case."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
DIAGNOSTICS = {
    "boot": ([], r"TASH>>"),
    "ps": (["ps"], r"PID\s*\|\s*PRIO"),
    "free": (["free"], r"total|Total"),
    "heapinfo": (["heapinfo"], r"Heap|heap|Total"),
    "heapinfo_all": (["heapinfo -a"], r"Allocated|alloc|HEAP|Heap"),
    "heapinfo_free": (["heapinfo -f"], r"Free|free|HEAP|Heap"),
    "heapinfo_regions": (["heapinfo -r"], r"[Rr]egion|[Hh]eap"),
    "stackmonitor": (["stkmon"], r"Stack Monitor:"),
    "network_interfaces": (["ifconfig"], r"eth0"),
    "histories": (["crashprobe histories"], r"TASK SCHEDULING HISTORY"),
    "assert": (["crashprobe assert"], r"Assertion failed at file:.*crashprobe"),
    "udf": (["crashprobe udf"], r"FAULT TYPE: UNDEFINSTR"),
    "divzero": (["crashprobe divzero"], r"FAULT TYPE: DIVBYZERO"),
    "unaligned": (["crashprobe unaligned"], r"FAULT TYPE: UNALIGNED"),
    "busfault": (["crashprobe busfault"], r"PANIC!!! Bus fault"),
    "hardfault": (["crashprobe hardfault"], r"PANIC!!! Hard fault"),
    "invstate": (["crashprobe invstate"], r"FAULT TYPE: INVSTATE"),
    "memfault": (["crashprobe memfault"], r"FAULT TYPE: DACCVIOL"),
    "execfault": (["crashprobe execfault"], r"FAULT TYPE: IACCVIOL"),
    "stack": (["crashprobe stack"], r"FAULT TYPE: STKOF"),
    "oom": (["crashprobe oom"], r"Allocation failed|allocation fail|alloc.*fail|FAIL.*alloc"),
    "doublefree": (["crashprobe doublefree"], r"WARNING!! Attempt for double freeing"),
    "heapcorrupt": (["crashprobe heapcorrupt"], r"ERROR: Heap corruption detected"),
    "kernel_tc": (["kernel_tc"], r"Kernel TC End \[PASS : [1-9][0-9]*, FAIL : 0\]"),
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def output(command: list[str]) -> str:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
    return (result.stdout if result.returncode == 0 else result.stderr).strip()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def capture(case: str, command: list[str], out: Path, timeout: float) -> dict:
    commands, expected = DIAGNOSTICS[case]
    begin = dt.datetime.now(dt.timezone.utc).isoformat()
    started = time.monotonic()
    proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    assert proc.stdout and proc.stdin
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    data = bytearray()
    command_events = []
    pending = list(commands)
    boot_seen = False
    registration_checked = False
    last_write = started
    last_read = started
    search_from = 0
    reason = "timeout"
    expected_seen = False
    fatal_seen = False
    return_before_stop = None
    try:
        while time.monotonic() - started < timeout:
            for key, _ in selector.select(0.05):
                chunk = os.read(key.fd, 65536)
                if chunk:
                    data.extend(chunk)
                    last_read = time.monotonic()
            now = time.monotonic()
            text = data.decode("utf-8", errors="replace")
            if not boot_seen and "TASH>>" in text:
                boot_seen = True
                last_write = now
            # Board application registration can finish shortly after first prompt.
            if boot_seen and not registration_checked and now - last_write >= 0.3:
                proc.stdin.write(b"help\n")
                proc.stdin.flush()
                command_events.append({"command": "help", "elapsed_seconds": round(now - started, 3)})
                search_from = len(data)
                registration_checked = True
                last_write = now
            elif registration_checked and "TASH>>" in text[search_from:] and now - last_write >= 0.15:
                if pending:
                    instruction = pending.pop(0)
                    proc.stdin.write((instruction + "\n").encode())
                    proc.stdin.flush()
                    command_events.append({"command": instruction, "elapsed_seconds": round(now - started, 3)})
                    search_from = len(data)
                    last_write = now
                elif not commands:
                    reason = "diagnostic-complete"
                    break
            relevant = text[search_from:] if commands else text
            expected_seen = re.search(expected, relevant, re.IGNORECASE) is not None
            fatal_seen = bool(re.search(r"Assertion failed at file:|PANIC!!!|Lockup: can't escalate", relevant))
            if expected_seen and not pending and now - last_read >= 0.5:
                if fatal_seen:
                    reason = "fault-observed-output-quiescent"
                    break
                if case == "stackmonitor" and now - last_write >= 2.5:
                    reason = "diagnostic-observation-window"
                    break
                if "TASH>>" in relevant:
                    reason = "diagnostic-complete"
                    break
            if proc.poll() is not None:
                reason = "qemu-exit"
                return_before_stop = proc.returncode
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                tail, _ = proc.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                tail, _ = proc.communicate(timeout=3)
            data.extend(tail or b"")
        selector.close()
    text = data.decode("utf-8", errors="replace")
    relevant = text[search_from:] if commands else text
    expected_seen = re.search(expected, relevant, re.IGNORECASE) is not None
    logfile = out / "raw" / f"{case}.log"
    logfile.write_bytes(data)
    result = {
        "case": case, "evidence_kind": "actual-qemu-runtime", "machine": "mps2-an505",
        "started_at_utc": begin, "elapsed_seconds": round(time.monotonic() - started, 3),
        "qemu_command": command, "serial_commands": command_events,
        "expected_pattern": expected, "expected_observed": expected_seen,
        "boot_prompt_observed": boot_seen, "termination_reason": reason,
        "observation_timed_out": reason == "timeout", "qemu_returncode_before_collector_stop": return_before_stop,
        "collector_stopped_qemu": return_before_stop is None, "qemu_final_returncode": proc.returncode,
        "assertion_observed": "Assertion failed at file:" in relevant,
        "architectural_lockup_observed": "Lockup: can't escalate" in relevant,
        "guest_reboot_requested": "Board will Reboot now" in relevant,
        "guest_prompt_after_fault": bool(re.search(r"(?:Assertion failed at file:|PANIC!!!).*TASH>>", relevant, re.DOTALL)),
        "log": str(logfile.relative_to(out)), "log_sha256": sha256(logfile), "log_bytes": len(data),
        "hardware_validated": False,
    }
    write_json(out / f"{case}.json", result)
    print(f"{case}: observed={expected_seen} reason={reason} bytes={len(data)}", flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="New directory; existing evidence is never overwritten")
    parser.add_argument("--case", choices=DIAGNOSTICS, action="append")
    parser.add_argument("--timeout", type=float, default=20)
    args = parser.parse_args()
    elf = ROOT / "build/output/bin/tinyara"
    config = ROOT / "os/.config"
    if not elf.is_file() or "CONFIG_QEMU_LOG_ANALYZER_PROBES=y" not in config.read_text():
        parser.error("build qemu-armv8m/log_analyzer via os/dbuild.sh menu first")
    if elf.stat().st_mtime < config.stat().st_mtime:
        parser.error(".config is newer than the ELF; rebuild before collecting")
    qemu = shutil.which("qemu-system-arm")
    if not qemu:
        parser.error("qemu-system-arm not found")
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "raw").mkdir()
    shutil.copy2(config, args.out / "kernel.config")
    if (ROOT / "os/build.log").is_file():
        shutil.copy2(ROOT / "os/build.log", args.out / "build.log")
    source_paths = [
        "os/arch/arm/src/qemu-armv8m/Kconfig", "os/board/qemu-armv8m/src/Makefile",
        "os/board/qemu-armv8m/src/qemu_armv8m_boot.c", "os/board/qemu-armv8m/src/qemu_armv8m_crashprobe.c",
        "build/configs/qemu-armv8m/log_analyzer/defconfig", "tools/crash-corpus/collect.py",
    ]
    source_hashes = {path: sha256(ROOT / path) for path in source_paths}
    # Include untracked new inputs as well as the tracked diff, so dirty-build
    # provenance stays reconstructable after subsequent source edits.
    for path in source_paths:
        target = args.out / "source" / path
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / path, target)
    symbols = ROOT / "build/output/bin/System.map"
    if symbols.is_file():
        shutil.copy2(symbols, args.out / "System.map")
    patch = output(["git", "diff", "--", *source_paths])
    (args.out / "source.patch").write_text(patch + "\n")
    write_json(args.out / "provenance.json", {
        "schema_version": 1, "evidence_kind": "actual-qemu-runtime", "source_root": str(ROOT),
        "git_head": output(["git", "rev-parse", "HEAD"]), "git_branch": output(["git", "branch", "--show-current"]),
        "git_dirty_paths": output(["git", "status", "--short"]), "source_sha256": source_hashes,
        "elf": str(elf), "elf_sha256": sha256(elf), "elf_size_bytes": elf.stat().st_size,
        "config_sha256": sha256(config), "qemu_version": output([qemu, "--version"]),
        "symbols_sha256": sha256(symbols) if symbols.is_file() else None,
        "docker_image": "tizenrt/tizenrt:2.0.1-arm64-local",
        "docker_image_id": output(["docker", "image", "inspect", "--format", "{{.Id}}", "tizenrt/tizenrt:2.0.1-arm64-local"]),
        "build_method": "Homebrew Bash os/dbuild.sh menu; qemu-armv8m -> log_analyzer -> 1",
        "hardware_validated": False, "synthetic_log": False,
    })
    command = [qemu, "-M", "mps2-an505", "-kernel", str(elf), "-nic", "user,model=lan9118", "-display", "none", "-serial", "stdio", "-monitor", "none"]
    # Full kernel_tc is an explicit longer regression, not a fast crash sample.
    cases = args.case or [case for case in DIAGNOSTICS if case != "kernel_tc"]
    results = [capture(case, command, args.out, args.timeout) for case in cases]
    write_json(args.out / "summary.json", {
        "evidence_kind": "actual-qemu-runtime", "cases": len(results),
        "observed": sum(result["expected_observed"] for result in results), "results": results,
        "scope": "Local MPS2-AN505 Cortex-M33 diagnostic fixtures; not hardware, full-kernel regression or universal log coverage",
    })
    return 0 if all(result["expected_observed"] for result in results) else 1


if __name__ == "__main__":
    sys.exit(main())
