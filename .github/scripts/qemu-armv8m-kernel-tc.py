#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from collections.abc import Callable
from dataclasses import replace
from pathlib import Path
from typing import Final, NoReturn

from qemu_armv8m_ab import (
    AbImageError,
    active_slot,
    extract_active_kernel,
    qemu_command as ab_qemu_command,
    stage_state,
    temporary_state,
)
from qemu_armv8m_protocol import PreflightError, RunRequest, run_protocol


SUPPORTED_CONFIGS: Final = ("hello", "loadable_all", "loadable_apps", "xip_all")
USER_NIC: Final = (
    "user,ipv4=on,ipv6=on,net=10.0.2.0/24,host=10.0.2.2,"
    "ipv6-net=fec0::/64,ipv6-host=fec0::2,hostname=tizenrt-qemu,"
    "mac=52:54:00:12:34:56"
)


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
    command.extend(["-nic", USER_NIC, "-display", "none", "-serial", "stdio", "-monitor", "none"])
    return command


def with_user_nic(command: list[str]) -> list[str]:
    if "-nic" in command:
        return command
    return [*command, "-nic", USER_NIC]


def command_for_request(request: RunRequest) -> list[str]:
    return qemu_command(
        request.root,
        request.config,
        common_path=request.common_path,
        app1_path=request.app1_path,
        omit_common=request.omit_common,
    )


def run_kernel_tc(request: RunRequest, command_builder: Callable[[RunRequest], list[str]] | None = None) -> int:
    if command_builder is not None or request.config == "hello":
        return run_protocol(request, command_for_request if command_builder is None else command_builder)
    return run_ab_kernel_tc(request)


def _attempt_path(path: Path, attempt: int) -> Path:
    if attempt == 0:
        return path
    return path.with_name(f"{path.stem}.attempt{attempt}{path.suffix}")


def _promote_result(request: RunRequest, state: Path, attempt: int) -> None:
    payload = json.loads(request.result_path.read_text(encoding="utf-8"))
    if request.state_image is not None:
        payload["state_image"] = str(state)
    payload["active_slot"] = active_slot(state, request.root, request.config)
    payload["attempt"] = attempt
    request.result_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _run_ab_attempts(request: RunRequest, state: Path) -> int:
    for attempt in range(request.max_reboots + 1):
        slot_before = active_slot(state, request.root, request.config)
        kernel = extract_active_kernel(request.root, request.config, state)
        attempt_request = replace(
            request,
            log_path=_attempt_path(request.log_path, attempt),
            result_path=_attempt_path(request.result_path, attempt),
        )
        code = run_protocol(attempt_request, lambda _request: with_user_nic(ab_qemu_command(state, kernel)))
        _promote_result(attempt_request, state, attempt)
        if code == 0 or attempt == request.max_reboots:
            if attempt_request.result_path != request.result_path:
                request.result_path.write_text(
                    attempt_request.result_path.read_text(encoding="utf-8"), encoding="utf-8"
                )
            return code

        result = json.loads(attempt_request.result_path.read_text(encoding="utf-8"))
        if result.get("reason") in ("preflight", "spawn"):
            return code
        if result.get("reason") != "qemu-exit" or result.get("returncode") != 0:
            return code
        if active_slot(state, request.root, request.config) == slot_before:
            return code

    return 1


def _raise_preflight(error: Exception) -> NoReturn:
    raise PreflightError(str(error))


def run_ab_kernel_tc(request: RunRequest) -> int:
    if request.max_reboots < 0:
        error = AbImageError("max reboots must not be negative")
        return run_protocol(request, lambda _request: _raise_preflight(error))

    try:
        if request.omit_common and request.config != "xip_all":
            raise PreflightError("--omit-common is only valid for xip_all")
        if request.common_path is not None and request.config != "xip_all":
            raise PreflightError("--common is only valid for xip_all")
        if request.app1_path is not None and request.config == "hello":
            raise PreflightError("--app1 is not valid for hello")
        custom_state = request.common_path is not None or request.app1_path is not None or request.omit_common
        if request.state_image is not None:
            state = request.state_image
            stage_state(
                request.root,
                request.config,
                state,
                force=custom_state,
                common_path=request.common_path,
                app1_path=request.app1_path,
                omit_common=request.omit_common,
            )
            attempt_request = replace(request, max_reboots=0) if request.expect_reject else request
            return _run_ab_attempts(attempt_request, state)

        with temporary_state() as directory:
            state = Path(directory) / "qemu.state"
            stage_state(
                request.root,
                request.config,
                state,
                common_path=request.common_path,
                app1_path=request.app1_path,
                omit_common=request.omit_common,
            )
            attempt_request = replace(request, max_reboots=0) if request.expect_reject else request
            return _run_ab_attempts(attempt_request, state)
    except (AbImageError, FileNotFoundError, PreflightError) as error:
        return run_protocol(request, lambda _request: _raise_preflight(error))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, choices=SUPPORTED_CONFIGS)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--result", type=Path)
    parser.add_argument("--common", type=Path)
    parser.add_argument("--app1", type=Path)
    parser.add_argument("--omit-common", action="store_true")
    parser.add_argument("--state-image", type=Path)
    parser.add_argument("--max-reboots", type=int, default=1)
    parser.add_argument("--expect-reject")
    parser.add_argument("--forbid-marker")
    parser.add_argument("--expect-once", action="append", default=[])
    parser.add_argument("--reject-observe-seconds", type=float, default=10)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    log_path = args.log or repo_root() / "build" / "qemu-armv8m" / f"{args.config}-kernel-tc.log"
    result_path = args.result or log_path.with_suffix(".result.json")
    request = RunRequest(
        config=args.config,
        root=repo_root(),
        timeout_sec=args.timeout,
        log_path=log_path,
        result_path=result_path,
        verbose=args.verbose,
        common_path=args.common,
        app1_path=args.app1,
        omit_common=args.omit_common,
        expect_reject=args.expect_reject,
        forbid_marker=args.forbid_marker,
        reject_observe_seconds=args.reject_observe_seconds,
        expected_once=tuple(args.expect_once),
        state_image=args.state_image,
        max_reboots=args.max_reboots,
        validate_network=True,
    )
    return run_kernel_tc(request)


if __name__ == "__main__":
    raise SystemExit(main())
