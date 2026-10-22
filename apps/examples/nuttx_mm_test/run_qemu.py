#!/usr/bin/env python3
"""Run the flat TASH allocator example once, preserving fresh serial evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import time

REVISION = "b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35"
CASES = ("public", "failure-preserves-data", "multiheap", "fragmentation", "threaded")
FAULT = re.compile(rb"Assertion failed|PANIC|up_assert:|HardFault|BusFault|UsageFault|qemu: fatal|NXMM RESULT FAIL")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output.resolve()
    elf = root / "build/output/bin/tinyara"
    config = root / "os/.config"
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if not elf.is_file() or not config.is_file():
        parser.error("build the configured firmware first")
    required = ("CONFIG_BUILD_FLAT=y", "CONFIG_MM_NUTTX=y", "CONFIG_EXAMPLES_NUTTX_MM_TEST=y")
    lines = set(config.read_text().splitlines())
    if any(option not in lines for option in required):
        parser.error("the firmware config must enable the flat NuttX test profile")
    if output.exists() and any(output.iterdir()):
        parser.error("--output must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    command = ["qemu-system-arm", "-M", "mps2-an505", "-kernel", str(elf),
               "-nic", "user,ipv4=on,ipv6=on,net=10.0.2.0/24,host=10.0.2.2,ipv6-net=fec0::/64,ipv6-host=fec0::2,mac=52:54:00:12:34:56",
               "-nographic", "-monitor", "none"]
    result = {"status": "fail", "firmware_sha256": digest(elf), "config_sha256": digest(config),
              "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              "git_status": subprocess.check_output(["git", "status", "--short"], cwd=root, text=True),
              "command": command, "backend_revision": REVISION}
    process = None
    selector = selectors.DefaultSelector()
    data = bytearray()
    cursor = 0
    started = time.monotonic()
    deadline = started + args.timeout
    log = (output / "serial.log").open("wb")

    def wait(pattern):
        nonlocal cursor
        regex = re.compile(pattern)
        while True:
            if FAULT.search(data):
                raise RuntimeError("target fault or test failure; inspect serial.log")
            match = regex.search(data, cursor)
            if match:
                segment = bytes(data[cursor:match.end()])
                cursor = match.end()
                return segment
            if time.monotonic() >= deadline:
                raise TimeoutError("missing fresh serial marker: " + repr(pattern))
            for key, _ in selector.select(0.2):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    raise RuntimeError("QEMU output closed")
                data.extend(chunk)
                log.write(chunk)
                log.flush()
            if process.poll() is not None:
                raise RuntimeError("QEMU exited before verification completed")

    def send(text):
        process.stdin.write((text + "\n").encode())
        process.stdin.flush()

    def shell(text):
        send(text)
        echoed = wait(re.escape(text.encode()) + rb"\r?\n")
        return echoed + wait(rb"TASH>>")

    try:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, bufsize=0)
        selector.register(process.stdout, selectors.EVENT_READ)
        wait(rb"TASH>>")
        registered = False
        for _ in range(10):
            if b"nuttx_mm_test" in shell("help"):
                registered = True
                break
            time.sleep(0.1)
        if not registered:
            raise RuntimeError("nuttx_mm_test is absent from TASH help")
        epoch = len(data)
        send("nuttx_mm_test")
        wait(rb"NXMM RESULT PASS passed=5 failed=0\r?\n")
        evidence = bytes(data[epoch:cursor])
        expected = ("NXMM BEGIN backend=12.13.0 revision=" + REVISION).encode()
        if expected not in evidence:
            raise RuntimeError("runtime backend identity mismatch")
        for case in CASES:
            pattern = (r"NXMM CASE " + re.escape(case) + r" PASS checks=([1-9][0-9]*) failures=0 line=0").encode()
            if not re.search(pattern, evidence):
                raise RuntimeError("missing positive case evidence: " + case)
        # The asynchronous command may print its summary just before return.
        for _ in range(20):
            snapshot = shell("ps")
            if not re.search(rb"\|\s*nuttx_mm_test\s*\r?$", snapshot, re.M):
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("test task did not exit")
        result.update(status="pass", cases=list(CASES), test_task_exited=True)
    except Exception as exc:
        result["error"] = str(exc)
    finally:
        if process is not None:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            tail = process.stdout.read()
            if tail:
                log.write(tail)
                data.extend(tail)
            result["qemu_exit_after_cleanup"] = process.returncode
        selector.close()
        log.close()
        if FAULT.search(data):
            result.update(status="fail", error="target fault or test failure; inspect serial.log")
        result["elapsed_seconds"] = round(time.monotonic() - started, 3)
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
