#!/usr/bin/env python3
#
# Run qemu-armv8m kernel_tc from a GitHub Actions job or a local checkout.

import argparse
import os
import re
import select
import subprocess
import sys
import time
from pathlib import Path


PASS_RE = re.compile(rb"Kernel TC End \[PASS : [0-9]+, FAIL : 0\]")
FAIL_RE = re.compile(rb"Kernel TC End \[PASS : [0-9]+, FAIL : [1-9][0-9]*\]")
PROMPT_RE = re.compile(rb"TASH>>")
NOT_REGISTERED_RE = re.compile(rb"TASH: cmd \(kernel_tc\) not registered")
PROGRESS_INTERVAL_SEC = 30
SUPPORTED_CONFIGS = ("hello", "loadable_all", "loadable_apps", "xip_all")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def qemu_command(root: Path, config: str) -> list[str]:
    bin_dir = root / "build" / "output" / "bin"
    tinyara = bin_dir / "tinyara"
    if not tinyara.is_file():
        raise FileNotFoundError(f"missing kernel image: {tinyara}")

    cmd = [
        "qemu-system-arm",
        "-M",
        "mps2-an505",
        "-kernel",
        str(tinyara),
    ]

    if config in ("loadable_all", "loadable_apps"):
        app1 = bin_dir / "app1"
        if not app1.is_file():
            raise FileNotFoundError(f"missing app package: {app1}")
        cmd.extend(["-device", f"loader,file={app1},addr=0x10300000,force-raw=on"])
    elif config == "xip_all":
        common = bin_dir / "common"
        app1 = bin_dir / "app1"
        if not common.is_file():
            raise FileNotFoundError(f"missing common package: {common}")
        if not app1.is_file():
            raise FileNotFoundError(f"missing app package: {app1}")
        cmd.extend(["-device", f"loader,file={common},addr=0x102c0000,force-raw=on"])
        cmd.extend(["-device", f"loader,file={app1},addr=0x10360000,force-raw=on"])
    elif config not in SUPPORTED_CONFIGS:
        raise ValueError(f"unsupported qemu-armv8m config: {config}")

    cmd.extend(["-display", "none", "-serial", "stdio", "-monitor", "none"])
    return cmd


def terminate(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def run_kernel_tc(config: str, timeout_sec: int, log_path: Path, verbose: bool) -> int:
    root = repo_root()
    cmd = qemu_command(root, config)

    log_path.parent.mkdir(parents=True, exist_ok=True)
    print("+ " + " ".join(cmd), flush=True)
    print(f"Writing QEMU serial log to {log_path}", flush=True)

    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=root,
        bufsize=0,
    )

    deadline = time.monotonic() + timeout_sec
    start = time.monotonic()
    search_window = bytearray()
    need_command = True
    send_count = 0
    last_send = 0.0
    last_activity = time.monotonic()
    last_progress = start
    total_bytes = 0

    def send_kernel_tc() -> int:
        nonlocal need_command, send_count, last_send
        if send_count >= 5:
            print("\nkernel_tc was not registered after 5 attempts", file=sys.stderr)
            return 1

        assert proc.stdin is not None
        proc.stdin.write(b"kernel_tc\n")
        proc.stdin.flush()
        need_command = False
        send_count += 1
        last_send = time.monotonic()
        print(f"qemu-armv8m/{config}: sent kernel_tc", flush=True)
        return 0

    try:
        with log_path.open("wb") as log:
            while time.monotonic() < deadline:
                if proc.stdout is None:
                    break

                now = time.monotonic()
                readable, _, _ = select.select([proc.stdout], [], [], 0.2)
                if not readable:
                    if proc.poll() is not None:
                        break
                    if need_command and now - start > 30 and now - last_send > 3:
                        if send_kernel_tc() != 0:
                            return 1
                    if not verbose and now - last_progress > PROGRESS_INTERVAL_SEC:
                        elapsed = int(now - start)
                        print(
                            f"qemu-armv8m/{config}: kernel_tc running "
                            f"({elapsed}s, {total_bytes} bytes logged)",
                            flush=True,
                        )
                        last_progress = now
                    continue

                chunk = os.read(proc.stdout.fileno(), 4096)
                if not chunk:
                    if proc.poll() is not None:
                        break
                    continue

                last_activity = time.monotonic()
                total_bytes += len(chunk)
                search_window.extend(chunk)
                if len(search_window) > 8192:
                    del search_window[:-8192]

                log.write(chunk)
                log.flush()

                if verbose:
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()

                now = time.monotonic()
                if not verbose and now - last_progress > PROGRESS_INTERVAL_SEC:
                    elapsed = int(now - start)
                    print(
                        f"qemu-armv8m/{config}: kernel_tc running "
                        f"({elapsed}s, {total_bytes} bytes logged)",
                        flush=True,
                    )
                    last_progress = now

                tail = bytes(search_window)
                should_send = PROMPT_RE.search(tail) or now - start > 30
                if need_command and should_send and now - last_send > 3:
                    if send_kernel_tc() != 0:
                        return 1

                if PASS_RE.search(tail):
                    print(f"qemu-armv8m/{config}: kernel_tc passed", flush=True)
                    return 0

                if FAIL_RE.search(tail):
                    print(f"qemu-armv8m/{config}: kernel_tc failed", file=sys.stderr)
                    return 1

                if NOT_REGISTERED_RE.search(tail):
                    need_command = True

            if send_count == 0 and proc.poll() is None:
                assert proc.stdin is not None
                proc.stdin.write(b"kernel_tc\n")
                proc.stdin.flush()

            if proc.poll() is not None:
                print(f"\nQEMU exited before kernel_tc passed: {proc.returncode}", file=sys.stderr)
            else:
                idle = int(time.monotonic() - last_activity)
                print(f"\nTimed out waiting for kernel_tc result after {timeout_sec}s, idle {idle}s", file=sys.stderr)
            return 1
    finally:
        terminate(proc)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, choices=SUPPORTED_CONFIGS)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    log_path = args.log
    if log_path is None:
        log_path = repo_root() / "build" / "qemu-armv8m" / f"{args.config}-kernel-tc.log"

    return run_kernel_tc(args.config, args.timeout, log_path, args.verbose)


if __name__ == "__main__":
    raise SystemExit(main())
