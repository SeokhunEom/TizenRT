#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import select
import signal
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path


REGISTRY_PATTERN = re.compile(r'\{ "(?P<command>ltp_t(?P<index>\d+))", ltp_runner_main,')
RESULT_PATTERN = re.compile(
    rb"LTP_RESULT (?P<command>ltp_t\d+) "
    rb"(?P<status>PASS|FAIL|UNRESOLVED|UNSUPPORTED|UNTESTED|UNKNOWN|ERROR)"
    rb"(?: exit=(?P<exit>\d+)|[^\r\n]*)\r?\n"
)
CRASH_MARKERS = (
    b"Assertion failed at file:",
    b"HardFault",
    b"PANIC!!!",
)
USER_NIC = (
    "user,ipv4=on,ipv6=on,net=10.0.2.0/24,host=10.0.2.2,"
    "ipv6-net=fec0::/64,ipv6-host=fec0::2,hostname=tizenrt-qemu,"
    "mac=52:54:00:12:34:56"
)


@dataclass(frozen=True)
class TestCase:
    index: int
    command: str
    function: str
    sources: tuple[str, ...]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def discover_tests(root: Path) -> list[TestCase]:
    registry = root / "apps" / "builtin" / "registry"
    manifest = root / "apps" / "examples" / "ltp" / "ltp_manifest.tsv"
    if not registry.is_dir() or not manifest.is_file():
        raise RuntimeError("LTP registry/manifest is missing; build qemu-armv8m/hello first")

    registered: set[str] = set()
    for metadata in registry.glob("ltp_*.mdat"):
        match = REGISTRY_PATTERN.search(metadata.read_text(encoding="utf-8"))
        if match is not None:
            registered.add(match.group("command"))

    tests: list[TestCase] = []
    for line in manifest.read_text(encoding="utf-8").splitlines():
        command, function, source = line.split("\t")
        match = re.fullmatch(r"ltp_t(\d+)", command)
        if match is None:
            raise RuntimeError(f"invalid LTP manifest command: {command}")
        tests.append(TestCase(int(match.group(1)), command, function, (source,)))

    tests.sort(key=lambda test: test.index)
    expected = list(range(1, len(tests) + 1))
    actual = [test.index for test in tests]
    if not tests or actual != expected:
        raise RuntimeError(f"LTP registry indices are not contiguous: {actual}")
    manifest_commands = {test.command for test in tests}
    if registered != manifest_commands:
        raise RuntimeError(
            f"LTP registry/manifest mismatch: registry={sorted(registered)}, "
            f"manifest={sorted(manifest_commands)}"
        )
    return tests


def append_serial(process: subprocess.Popen[bytes], log_file, timeout: float) -> bytes:
    ready, _, _ = select.select([process.stdout], [], [], timeout)
    if not ready:
        return b""
    assert process.stdout is not None
    chunk = os.read(process.stdout.fileno(), 4096)
    if chunk:
        log_file.write(chunk)
        log_file.flush()
    return chunk


def terminate(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=3)


def wait_for_boot(process: subprocess.Popen[bytes], log_file, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    output = bytearray()
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited during boot with status {process.returncode}")
        output.extend(append_serial(process, log_file, min(0.5, deadline - time.monotonic())))
        if b"TASH>>" in output:
            return
        if any(marker in output for marker in CRASH_MARKERS):
            raise RuntimeError("target crashed during boot")
    raise RuntimeError("timed out waiting for TASH prompt")


def run_test(
    process: subprocess.Popen[bytes], log_file, test: TestCase, timeout: float
) -> dict[str, object]:
    assert process.stdin is not None
    process.stdin.write(f"{test.command}\n".encode())
    process.stdin.flush()

    started = time.monotonic()
    deadline = started + timeout
    output = bytearray()
    outcome = "timeout"
    exit_status: int | None = None

    while time.monotonic() < deadline:
        if process.poll() is not None:
            outcome = "qemu-exit"
            break
        output.extend(append_serial(process, log_file, min(0.5, deadline - time.monotonic())))
        if any(marker in output for marker in CRASH_MARKERS):
            outcome = "crash"
            break
        for match in RESULT_PATTERN.finditer(output):
            if match.group("command").decode() != test.command:
                continue
            outcome = match.group("status").decode().lower()
            if match.group("exit") is not None:
                exit_status = int(match.group("exit"))
            break
        if outcome not in ("timeout",):
            break

    return {
        **asdict(test),
        "status": outcome,
        "exit_status": exit_status,
        "duration_sec": round(time.monotonic() - started, 3),
        "output": output.decode("utf-8", errors="replace").replace("\x00", ""),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--boot-timeout", type=float, default=60)
    parser.add_argument("--test-timeout", type=float, default=30)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--result", type=Path)
    parser.add_argument("--tests", nargs="*", type=int)
    args = parser.parse_args()

    root = repo_root()
    log_path = args.log or root / "build" / "qemu-armv8m-ltp" / "ltp-serial.log"
    result_path = args.result or log_path.with_suffix(".result.json")
    tinyara = root / "build" / "output" / "bin" / "tinyara"
    if not tinyara.is_file():
        parser.error(f"missing kernel image: {tinyara}")

    tests = discover_tests(root)
    if args.tests:
        selected = set(args.tests)
        tests = [test for test in tests if test.index in selected]
        missing = sorted(selected - {test.index for test in tests})
        if missing:
            parser.error(f"unknown test indices: {missing}")

    log_path.parent.mkdir(parents=True, exist_ok=True)
    result_path.parent.mkdir(parents=True, exist_ok=True)
    command = [
        "qemu-system-arm",
        "-M",
        "mps2-an505",
        "-kernel",
        str(tinyara),
        "-nic",
        USER_NIC,
        "-display",
        "none",
        "-serial",
        "stdio",
        "-monitor",
        "none",
    ]
    results: list[dict[str, object]] = []
    run_error: str | None = None

    with log_path.open("wb") as log_file:
        process = subprocess.Popen(
            command,
            cwd=root,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            wait_for_boot(process, log_file, args.boot_timeout)
            for position, test in enumerate(tests, start=1):
                result = run_test(process, log_file, test, args.test_timeout)
                results.append(result)
                print(
                    f"[{position:03d}/{len(tests):03d}] "
                    f"{test.command}: {result['status']} ({result['duration_sec']}s)",
                    flush=True,
                )
                if result["status"] in ("crash", "qemu-exit", "timeout"):
                    break
        except Exception as error:  # Preserve partial results and diagnostics.
            run_error = str(error)
        finally:
            terminate(process)

    passed = sum(result["status"] == "pass" for result in results)
    payload = {
        "status": "pass" if run_error is None and len(results) == len(tests) and passed == len(tests) else "fail",
        "command": command,
        "kernel": str(tinyara.relative_to(root)),
        "tests_selected": len(tests),
        "tests_run": len(results),
        "passed": passed,
        "failed": len(results) - passed,
        "run_error": run_error,
        "tests": results,
    }
    result_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({key: payload[key] for key in ("status", "tests_selected", "tests_run", "passed", "failed", "run_error")}))
    return 0 if payload["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
