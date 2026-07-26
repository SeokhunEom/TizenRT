#!/usr/bin/env python3

from __future__ import annotations

import argparse
from collections.abc import Callable
from pathlib import Path
from typing import Final

from qemu_armv8m_protocol import PreflightError, RunRequest, run_protocol


SUPPORTED_CONFIGS: Final = ("hello", "loadable_all", "loadable_apps", "xip_all")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def qemu_command(
    root: Path,
    config: str,
    *,
    common_path: Path | None = None,
    app1_path: Path | None = None,
    omit_common: bool = False,
) -> list[str]:
    if config not in SUPPORTED_CONFIGS:
        raise PreflightError(f"unsupported qemu-armv8m config: {config}")
    if omit_common and config != "xip_all":
        raise PreflightError("--omit-common is only valid for xip_all")
    if common_path is not None and config != "xip_all":
        raise PreflightError("--common is only valid for xip_all")
    if app1_path is not None and config == "hello":
        raise PreflightError("--app1 is not valid for hello")
    bin_dir = root / "build" / "output" / "bin"
    tinyara = bin_dir / "tinyara"
    if not tinyara.is_file():
        raise FileNotFoundError(f"missing kernel image: {tinyara}")
    command = ["qemu-system-arm", "-M", "mps2-an505", "-kernel", str(tinyara)]
    if config in ("loadable_all", "loadable_apps"):
        app1 = app1_path or bin_dir / "app1"
        if not app1.is_file():
            raise FileNotFoundError(f"missing app package: {app1}")
        command.extend(["-device", f"loader,file={app1},addr=0x10300000,force-raw=on"])
    if config == "xip_all":
        common = common_path or bin_dir / "common"
        app1 = app1_path or bin_dir / "app1"
        if not omit_common:
            if not common.is_file():
                raise FileNotFoundError(f"missing common package: {common}")
            command.extend(["-device", f"loader,file={common},addr=0x102c0000,force-raw=on"])
        if not app1.is_file():
            raise FileNotFoundError(f"missing app package: {app1}")
        command.extend(["-device", f"loader,file={app1},addr=0x10360000,force-raw=on"])
    command.extend(["-display", "none", "-serial", "stdio", "-monitor", "none"])
    return command


def command_for_request(request: RunRequest) -> list[str]:
    return qemu_command(
        request.root,
        request.config,
        common_path=request.common_path,
        app1_path=request.app1_path,
        omit_common=request.omit_common,
    )


def run_kernel_tc(request: RunRequest, command_builder: Callable[[RunRequest], list[str]] | None = None) -> int:
    return run_protocol(request, command_for_request if command_builder is None else command_builder)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, choices=SUPPORTED_CONFIGS)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--result", type=Path)
    parser.add_argument("--common", type=Path)
    parser.add_argument("--app1", type=Path)
    parser.add_argument("--omit-common", action="store_true")
    parser.add_argument("--expect-reject")
    parser.add_argument("--forbid-marker")
    parser.add_argument("--expect-once", action="append", default=[])
    parser.add_argument("--reject-observe-seconds", type=float, default=10)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    log_path = args.log or repo_root() / "build" / "qemu-armv8m" / f"{args.config}-kernel-tc.log"
    result_path = args.result or log_path.with_suffix(".result.json")
    request = RunRequest(
        args.config,
        repo_root(),
        args.timeout,
        log_path,
        result_path,
        args.verbose,
        args.common,
        args.app1,
        args.omit_common,
        args.expect_reject,
        args.forbid_marker,
        args.reject_observe_seconds,
        tuple(args.expect_once),
    )
    return run_kernel_tc(request)


if __name__ == "__main__":
    raise SystemExit(main())
