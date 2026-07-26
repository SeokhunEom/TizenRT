from __future__ import annotations

import json
import math
import os
import re
import select
import subprocess
import sys
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from qemu_armv8m_prompt import PromptEpoch


PASS_RE: Final = re.compile(rb"Kernel TC End \[PASS : ([0-9]+), FAIL : ([0-9]+)\]")
MAX_SEND_COUNT: Final = 5
MAX_REJECT_OBSERVE_SECONDS: Final = 60.0
TERMINAL_REAP_SECONDS: Final = 0.1
SERIAL_WINDOW_BYTES: Final = 8192
FAILURE_TAIL_BYTES: Final = 4096
JsonValue = bool | int | str | None


@dataclass(frozen=True, slots=True)
class RunRequest:
    config: str
    root: Path
    timeout_sec: float
    log_path: Path
    result_path: Path
    verbose: bool
    common_path: Path | None
    app1_path: Path | None
    omit_common: bool
    expect_reject: str | None
    forbid_marker: str | None
    reject_observe_seconds: float
    expected_once: tuple[str, ...] = ()


class PreflightError(ValueError):
    pass


class ProtocolState:
    __slots__ = (
        "window",
        "send_count",
        "command_prompt",
        "required_marker_seen",
        "forbidden_marker_seen",
        "observe_deadline",
        "pass_count",
        "fail_count",
    )

    def __init__(self) -> None:
        self.window = bytearray()
        self.send_count = 0
        self.command_prompt = PromptEpoch()
        self.required_marker_seen = False
        self.forbidden_marker_seen = False
        self.observe_deadline: float | None = None
        self.pass_count: int | None = None
        self.fail_count: int | None = None


@dataclass(frozen=True, slots=True)
class ProtocolOutcome:
    status: str
    reason: str
    returncode: int | None


CommandBuilder = Callable[[RunRequest], list[str]]


def validate_request(request: RunRequest) -> str | None:
    if not math.isfinite(request.timeout_sec) or request.timeout_sec <= 0:
        return "timeout must be positive"
    if not 0 < request.reject_observe_seconds <= MAX_REJECT_OBSERVE_SECONDS:
        return f"reject observation seconds must be within (0, {MAX_REJECT_OBSERVE_SECONDS:g}]"
    if (request.expect_reject is None) != (request.forbid_marker is None):
        return "expected rejection requires both --expect-reject and --forbid-marker"
    if request.expect_reject is not None and not request.expect_reject.strip():
        return "expected rejection marker must not be empty"
    if request.forbid_marker is not None and not request.forbid_marker.strip():
        return "forbidden marker must not be empty"
    if any(not marker.strip() for marker in request.expected_once):
        return "exact-once markers must not be empty"
    return None


def write_result(request: RunRequest, state: ProtocolState, outcome: ProtocolOutcome) -> None:
    payload: dict[str, JsonValue] = {
        "status": outcome.status,
        "reason": outcome.reason,
        "returncode": outcome.returncode,
        "config": request.config,
        "log_path": str(request.log_path),
        "pass_count": state.pass_count,
        "fail_count": state.fail_count,
        "send_count": state.send_count,
        "required_marker": request.expect_reject,
        "required_marker_seen": state.required_marker_seen,
        "forbidden_marker": request.forbid_marker,
        "forbidden_marker_seen": state.forbidden_marker_seen,
    }
    request.result_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def finish(request: RunRequest, state: ProtocolState, outcome: ProtocolOutcome) -> int:
    write_result(request, state, outcome)
    if outcome.status in ("pass", "expected-rejection"):
        return 0
    print(f"qemu-armv8m/{request.config}: {outcome.reason}", file=sys.stderr)
    tail = bytes(state.window[-FAILURE_TAIL_BYTES:]).decode("utf-8", errors="replace").rstrip()
    if tail:
        print(f"qemu-armv8m/{request.config}: serial tail:\n{tail}", file=sys.stderr)
    return 1


def terminate(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def close_streams(process: subprocess.Popen[bytes]) -> None:
    if process.stdin is not None:
        try:
            process.stdin.close()
        except BrokenPipeError:
            if process.stdout is not None:
                process.stdout.close()
            return
    if process.stdout is not None:
        process.stdout.close()


def positive_terminal(request: RunRequest, process: subprocess.Popen[bytes], state: ProtocolState) -> ProtocolOutcome | None:
    match = PASS_RE.search(bytes(state.window))
    if match is None:
        return None
    state.pass_count = int(match.group(1))
    state.fail_count = int(match.group(2))
    if state.fail_count > 0:
        return ProtocolOutcome("failed", "kernel-tc-fail", None)
    if state.pass_count == 0:
        return ProtocolOutcome("failed", "protocol-error", None)
    serial = request.log_path.read_bytes()
    invalid_markers = [marker for marker in request.expected_once if serial.count(marker.encode()) != 1]
    if invalid_markers:
        return ProtocolOutcome("failed", "marker-count", None)
    try:
        returncode = process.wait(timeout=TERMINAL_REAP_SECONDS)
    except subprocess.TimeoutExpired:
        return ProtocolOutcome("pass", "pass", None)
    if returncode != 0:
        return ProtocolOutcome("failed", "qemu-exit", returncode)
    return ProtocolOutcome("pass", "pass", returncode)


def send_kernel_tc(process: subprocess.Popen[bytes], state: ProtocolState) -> ProtocolOutcome | None:
    if state.send_count >= MAX_SEND_COUNT:
        return ProtocolOutcome("failed", "protocol-error", process.poll())
    if process.stdin is None:
        return ProtocolOutcome("failed", "protocol-error", process.poll())
    try:
        process.stdin.write(b"kernel_tc\n")
        process.stdin.flush()
    except BrokenPipeError:
        return ProtocolOutcome("failed", "qemu-exit", process.poll())
    state.send_count += 1
    return None


def update_negative_markers(request: RunRequest, state: ProtocolState, now: float) -> ProtocolOutcome | None:
    if request.expect_reject is None or request.forbid_marker is None:
        return None
    output = bytes(state.window)
    if request.expect_reject.encode() in output:
        state.required_marker_seen = True
    if request.forbid_marker.encode() in output:
        state.forbidden_marker_seen = True
    if state.forbidden_marker_seen:
        return ProtocolOutcome("failed", "protocol-error", None)
    if state.required_marker_seen and state.observe_deadline is None:
        state.observe_deadline = now + request.reject_observe_seconds
    return None


def initialize_artifacts(request: RunRequest, state: ProtocolState) -> None:
    request.log_path.parent.mkdir(parents=True, exist_ok=True)
    request.result_path.parent.mkdir(parents=True, exist_ok=True)
    request.log_path.write_bytes(b"")
    write_result(request, state, ProtocolOutcome("failed", "preflight", None))


def run_protocol(request: RunRequest, command_builder: CommandBuilder) -> int:
    state = ProtocolState()
    initialize_artifacts(request, state)
    validation_error = validate_request(request)
    if validation_error is not None:
        print(f"qemu-armv8m/{request.config}: {validation_error}", file=sys.stderr)
        return finish(request, state, ProtocolOutcome("failed", "preflight", None))
    try:
        command = command_builder(request)
    except (FileNotFoundError, PreflightError) as error:
        print(f"qemu-armv8m/{request.config}: preflight: {error}", file=sys.stderr)
        return finish(request, state, ProtocolOutcome("failed", "preflight", None))
    print("+ " + " ".join(command), flush=True)
    print(f"Writing QEMU serial log to {request.log_path}", flush=True)
    try:
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            cwd=request.root,
            bufsize=0,
        )
    except OSError as error:
        print(f"qemu-armv8m/{request.config}: spawn: {error}", file=sys.stderr)
        return finish(request, state, ProtocolOutcome("failed", "spawn", None))

    deadline = time.monotonic() + request.timeout_sec
    try:
        with request.log_path.open("ab") as log:
            while True:
                now = time.monotonic()
                if now >= deadline:
                    return finish(request, state, ProtocolOutcome("failed", "timeout", process.poll()))
                if state.observe_deadline is not None:
                    if process.poll() is not None:
                        return finish(request, state, ProtocolOutcome("failed", "qemu-exit", process.returncode))
                    if now >= state.observe_deadline:
                        return finish(request, state, ProtocolOutcome("expected-rejection", "expected-rejection", None))
                if process.stdout is None:
                    return finish(request, state, ProtocolOutcome("failed", "protocol-error", process.poll()))
                wait_seconds = min(0.1, deadline - now)
                readable, _, _ = select.select([process.stdout], [], [], wait_seconds)
                now = time.monotonic()
                if now >= deadline:
                    return finish(request, state, ProtocolOutcome("failed", "timeout", process.poll()))
                if not readable:
                    if process.poll() is not None:
                        return finish(request, state, ProtocolOutcome("failed", "qemu-exit", process.returncode))
                    continue
                chunk = os.read(process.stdout.fileno(), 4096)
                if not chunk:
                    if process.poll() is not None:
                        return finish(request, state, ProtocolOutcome("failed", "qemu-exit", process.returncode))
                    continue
                state.window.extend(chunk)
                if len(state.window) > SERIAL_WINDOW_BYTES:
                    del state.window[:-SERIAL_WINDOW_BYTES]
                log.write(chunk)
                log.flush()
                if request.verbose:
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()
                negative_outcome = update_negative_markers(request, state, now)
                if negative_outcome is not None:
                    return finish(request, state, negative_outcome)
                if request.expect_reject is not None:
                    continue
                if state.command_prompt.consumes_fresh_prompt(chunk):
                    send_outcome = send_kernel_tc(process, state)
                    if send_outcome is not None:
                        return finish(request, state, send_outcome)
                terminal = positive_terminal(request, process, state)
                if terminal is not None:
                    return finish(request, state, terminal)
    finally:
        terminate(process)
        close_streams(process)
