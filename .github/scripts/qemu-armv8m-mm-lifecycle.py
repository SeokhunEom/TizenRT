#!/usr/bin/env python3
"""Verify real app2 load/normal-exit/reload through fresh protected QEMU TASH."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import time

from qemu_armv8m_ab import extract_active_kernel, qemu_command, stage_state

REVISION = "b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35"
FAULT = re.compile(rb"Assertion failed|PANIC|up_assert:|HardFault|BusFault|UsageFault|qemu: fatal|NXMM (?:APP2|LIFE)[^\r\n]*FAIL")
CYCLE = re.compile(rb"NXMM LIFE CYCLE PASS round=(\d+) main_pid=(\d+) child_pid=(\d+) group_pid=(\d+) heap=(0x[0-9a-fA-F]+) ownership=1 init=1 destroy=1 registry=0 common=0 delayed=0 heap_restored=1\r?\n")
MAIN = re.compile(rb"NXMM APP2 MAIN PASS pid=(\d+) live_bytes=257 normal_exit=1\r?\n")
CHILD = re.compile(rb"NXMM APP2 PASS pid=(\d+) after_main=1 final_child=1\r?\n")
GROUP = re.compile(rb"NXMM APP2 GROUP PASS pid=(\d+) after_main=1 after_child=1 separate_group=1\r?\n")
# TASH may appear before the loader and app2 finish printing. Consume the
# complete buffered boot transcript only after all three events, in any order.
BOOT_READY = re.compile(
    rb"(?s)(?=.*TASH>>)(?=.*binary_manager_load_binary: Load success! \[Name: app1\][^\n]*\n)"
    rb"(?=.*" + GROUP.pattern + rb").*")


def round_start_pattern(number: int) -> bytes:
    return ("NXMM LIFE ROUND BEGIN round=" + str(number) + "\r?\n").encode()


def validate_round(segment: bytes, expected_round: int) -> dict:
    """Discard warm-up/previous-round output only before the explicit boundary."""
    if FAULT.search(segment):
        raise ValueError("target failure before or during lifecycle round")
    starts = list(re.finditer(round_start_pattern(expected_round), segment))
    if len(starts) != 1:
        raise ValueError("missing or duplicate round-start boundary")
    return validate_cycle(segment[starts[0].end():], expected_round)


def validate_cycle(segment: bytes, expected_round: int) -> dict:
    """Require fresh main, late pthread, separate group and cleanup evidence."""
    if FAULT.search(segment):
        raise ValueError("target failure in lifecycle evidence")
    cycles, mains, children = list(CYCLE.finditer(segment)), list(MAIN.finditer(segment)), list(CHILD.finditer(segment))
    groups = list(GROUP.finditer(segment))
    if len(cycles) != 1 or len(mains) != 1 or len(children) != 1 or len(groups) != 1:
        raise ValueError("missing or duplicate lifecycle evidence")
    cycle, main, child, group = cycles[0], mains[0], children[0], groups[0]
    number, main_pid, child_pid, group_pid = map(int, cycle.group(1, 2, 3, 4))
    if (number != expected_round or int(main.group(1)) != main_pid or
            int(child.group(1)) != child_pid or int(group.group(1)) != group_pid):
        raise ValueError("lifecycle round/PID mismatch")
    if len({main_pid, child_pid, group_pid}) != 3 or not main.start() < child.start() < group.start() < cycle.start():
        raise ValueError("main/final-child/separate-group/cleanup order mismatch")
    return {"round": number, "main_pid": main_pid, "child_pid": child_pid,
            "group_pid": group_pid, "heap": cycle.group(5).decode(), "live_allocation_in_app_heap": True,
            "initialized": 1, "uninitialized": 1,
            "app_registry_entries": 0, "common_heap_slot": 0, "delayed_references": 0,
            "kernel_heap_restored": True}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--config", required=True, choices=("loadable_all", "loadable_apps"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=240)
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    config = root / "os/.config"
    values = dict(line.split("=", 1) for line in config.read_text().splitlines() if line.startswith("CONFIG_") and "=" in line)
    required = ("CONFIG_BUILD_PROTECTED", "CONFIG_APP_BINARY_SEPARATION", "CONFIG_BINARY_MANAGER",
                "CONFIG_SUPPORT_COMMON_BINARY", "CONFIG_MM_NUTTX", "CONFIG_EXAMPLES_NUTTX_MM_LIFECYCLE")
    if any(values.get(key) != "y" for key in required) or values.get("CONFIG_NUM_APPS") != "2":
        parser.error("build the protected two-app NuttX lifecycle profile first")
    if any(values.get(key) == "y" for key in ("CONFIG_BINMGR_RECOVERY", "CONFIG_OPTIMIZE_APP_RELOAD_TIME", "CONFIG_XIP_ELF")):
        parser.error("fault recovery, retained-arena reload and XIP ELF are outside this test")
    if (values.get("CONFIG_XIP_KERNEL") == "y") != (args.config == "loadable_apps"):
        parser.error("--config does not match the generated firmware configuration")
    if values.get("CONFIG_ARCH_BOARD") != '"qemu-armv8m"' or args.timeout <= 0:
        parser.error("a QEMU ARMv8-M profile and positive timeout are required")
    if output.exists() and any(output.iterdir()):
        parser.error("--output must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    packages = {name: root / "build/output/bin" / name for name in ("tinyara", "tinyara.bin", "common", "app1", "app2")}
    result = {"status": "fail", "config": args.config, "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              "git_status": subprocess.check_output(["git", "status", "--short"], cwd=root, text=True),
              "config_sha256": sha256(config), "packages_sha256": {name: sha256(path) for name, path in packages.items()},
              "backend_revision": REVISION, "cycles": []}
    state = output / "ram.state"
    stage_state(root, args.config, state)
    command = qemu_command(state, extract_active_kernel(root, args.config, state))
    command.extend(["-nic", "user,ipv4=on,ipv6=on,net=10.0.2.0/24,host=10.0.2.2,ipv6-net=fec0::/64,ipv6-host=fec0::2,mac=52:54:00:12:34:56"])
    result["command"] = command
    selector = selectors.DefaultSelector()
    data = bytearray()
    cursor = 0
    process = None
    started = time.monotonic()
    deadline = started + args.timeout
    log = (output / "serial.log").open("wb")

    def wait(pattern: bytes) -> bytes:
        nonlocal cursor
        regex = re.compile(pattern)
        while True:
            if FAULT.search(data):
                raise RuntimeError("target fault or lifecycle failure; inspect serial.log")
            match = regex.search(data, cursor)
            if match:
                segment = bytes(data[cursor:match.end()])
                cursor = match.end()
                return segment
            if time.monotonic() >= deadline:
                raise TimeoutError("missing fresh lifecycle marker: " + repr(pattern))
            for key, _ in selector.select(0.2):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    raise RuntimeError("QEMU output closed")
                data.extend(chunk)
                log.write(chunk)
                log.flush()
            if process.poll() is not None:
                raise RuntimeError("QEMU exited before lifecycle completion")

    def send(text: str) -> None:
        process.stdin.write((text + "\n").encode())
        process.stdin.flush()

    def shell(text: str) -> bytes:
        send(text)
        return wait(re.escape(text.encode()) + rb"\r?\n") + wait(rb"TASH>>")

    try:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
        selector.register(process.stdout, selectors.EVENT_READ)
        wait(BOOT_READY.pattern)
        for _ in range(20):
            if b"nuttx_mm_life" in shell("help"):
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("nuttx_mm_life is absent from TASH help")
        send("nuttx_mm_life")
        wait(("NXMM LIFE BEGIN backend=12.13.0 revision=" + REVISION + " cycles=3\r?\n").encode())
        for number in range(1, 4):
            start = wait(round_start_pattern(number))
            result["cycles"].append(validate_round(start + wait(CYCLE.pattern), number))
        wait(rb"NXMM LIFE RESULT PASS cycles=3\r?\n")
        for _ in range(20):
            snapshot = shell("ps")
            if not re.search(rb"\|\s*nuttx_mm_life\s*\r?$", snapshot, re.M):
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("lifecycle controller task did not exit")
        result["test_task_exited"] = True
        result["status"] = "pass"
    except Exception as error:
        result["error"] = str(error)
    finally:
        if process is not None:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            result["qemu_exit_after_cleanup"] = process.returncode
            process.stdin.close()
            process.stdout.close()
        selector.close()
        log.close()
        result["elapsed_seconds"] = round(time.monotonic() - started, 3)
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
