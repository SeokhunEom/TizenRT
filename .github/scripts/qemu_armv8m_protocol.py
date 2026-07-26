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
NETWORK_PASS_RE: Final = re.compile(rb"Network TC End \[PASS : ([0-9]+), FAIL : ([0-9]+)\]")
PING_RE: Final = re.compile(rb"([0-9]+) packets transmitted, ([0-9]+) received")
DHCP_RE: Final = re.compile(rb"get IP address (10\.0\.2\.[0-9]+)")
IFCONFIG_IPV4_RE: Final = re.compile(rb"inet(?: addr)?:?\s*(10\.0\.2\.[0-9]+)", re.IGNORECASE)
KERNEL_ASSERT_RE: Final = re.compile(
    rb"Assertion failed at file:[^\r\n]*\bline:\s*[0-9]+"
)
MAX_SEND_COUNT: Final = 5
MAX_REJECT_OBSERVE_SECONDS: Final = 60.0
TERMINAL_REAP_SECONDS: Final = 0.1
SERIAL_WINDOW_BYTES: Final = 8192
FAILURE_TAIL_BYTES: Final = 4096
PHASE_OUTPUT_BYTES: Final = 65536
NETWORK_COMMANDS: Final = (
    b"ifconfig",
    b"ifdown",
    b"ifup",
    b"ping",
    b"ping6",
    b"netmon",
    b"net_stats",
    b"netdb",
    b"network_tc",
    b"kernel_tc",
)
JsonValue = bool | int | str | None | dict[str, object] | list[object]


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
    state_image: Path | None = None
    max_reboots: int = 0
    validate_network: bool = False


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
        "network",
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
        self.network = NetworkState()


class NetworkState:
    __slots__ = (
        "phase",
        "phase_output",
        "prompt_tail",
        "retry_count",
        "ipv4",
        "pings",
        "dns_resolved",
        "network_tc_pass_count",
        "network_tc_fail_count",
        "commands",
    )

    def __init__(self) -> None:
        self.phase = -1
        self.phase_output = bytearray()
        self.prompt_tail = bytearray()
        self.retry_count = 0
        self.ipv4: str | None = None
        self.pings: dict[str, dict[str, int]] = {}
        self.dns_resolved = False
        self.network_tc_pass_count: int | None = None
        self.network_tc_fail_count: int | None = None
        self.commands: list[str] = []


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
        "network": network_result(state.network, outcome) if request.validate_network and request.expect_reject is None else None,
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


def network_result(network: NetworkState, outcome: ProtocolOutcome) -> dict[str, object]:
    passed = (
        network.network_tc_pass_count is not None
        and network.network_tc_pass_count > 0
        and network.network_tc_fail_count == 0
    )
    failure_reason = outcome.reason if outcome.reason.startswith("network-") else None
    return {
        "status": "pass" if passed else ("failed" if failure_reason is not None else "not-run"),
        "failure_reason": failure_reason,
        "commands": network.commands,
        "retry_count": network.retry_count,
        "ipv4": network.ipv4,
        "pings": network.pings,
        "dns_resolved": network.dns_resolved,
        "network_tc_pass_count": network.network_tc_pass_count,
        "network_tc_fail_count": network.network_tc_fail_count,
    }


def _network_sequence() -> tuple[tuple[str, bytes], ...]:
    return (
        ("settle", b"sleep 1\n"),
        ("help", b"help\n"),
        ("ifdown", b"ifdown eth0\n"),
        ("ifup", b"ifup eth0\n"),
        ("dhcp", b"ifconfig eth0 dhcp\n"),
        ("ipv6-config", b"ifconfig eth0 fec0::15\n"),
        ("ipv6-settle", b"sleep 2\n"),
        ("ifconfig", b"ifconfig eth0\n"),
        ("gateway-ping", b"ping -c 3 10.0.2.2\n"),
        ("gateway-ping6", b"ping6 -c 3 fec0::2\n"),
        ("dns", b"netdb --host example.com\n"),
        ("public-ping", b"ping -c 1 1.1.1.1\n"),
        ("net-stats", b"net_stats\n"),
        ("network-tc", b"network_tc\n"),
        ("network-settle", b"sleep 2\n"),
        ("kernel-tc", b"kernel_tc\n"),
    )


def _send_network_command(
    process: subprocess.Popen[bytes],
    state: ProtocolState,
    phase: int,
) -> ProtocolOutcome | None:
    sequence = _network_sequence()
    if phase >= len(sequence) or process.stdin is None:
        return ProtocolOutcome("failed", "protocol-error", process.poll())
    if state.send_count >= 32:
        return ProtocolOutcome("failed", "protocol-error", process.poll())
    name, command = sequence[phase]
    try:
        process.stdin.write(command)
        process.stdin.flush()
    except BrokenPipeError:
        return ProtocolOutcome("failed", "qemu-exit", process.poll())
    state.network.phase = phase
    state.network.phase_output.clear()
    state.network.commands.append(command.decode().strip())
    state.send_count += 1
    return None


def _restart_network(process: subprocess.Popen[bytes], state: ProtocolState) -> ProtocolOutcome | None:
    if state.network.retry_count >= 1:
        return None
    state.network.retry_count += 1
    state.network.ipv4 = None
    state.network.dns_resolved = False
    state.network.pings.clear()
    return _send_network_command(process, state, 2)


def _prompt_count(network: NetworkState, chunk: bytes) -> int:
    tail_length = len(network.prompt_tail)
    serial = bytes(network.prompt_tail) + chunk
    count = sum(1 for match in re.finditer(rb"TASH>>", serial) if match.end() > tail_length)
    network.prompt_tail[:] = serial[-5:]
    return count


def _phase_failure_reason(name: str) -> str:
    return {
        "help": "network-command-registration",
        "settle": "network-command-registration",
        "ifdown": "network-ifdown",
        "ifup": "network-ifup",
        "dhcp": "network-dhcp",
        "ipv6-config": "network-ipv6-config",
        "ipv6-settle": "network-ipv6-config",
        "ifconfig": "network-dhcp",
        "gateway-ping": "network-ipv4-ping",
        "gateway-ping6": "network-ipv6-ping",
        "dns": "network-dns",
        "public-ping": "network-public-ping",
        "net-stats": "network-stats",
        "network-tc": "network-tc-fail",
        "network-settle": "network-tc-fail",
        "kernel-tc": "kernel-tc-fail",
    }[name]


def _ping_result(output: bytes) -> tuple[int, int] | None:
    match = PING_RE.search(output)
    if match is None:
        return None
    return int(match.group(1)), int(match.group(2))


def _phase_complete(name: str, output: bytes, prompt_seen: bool) -> bool:
    if name in ("gateway-ping", "gateway-ping6", "public-ping"):
        return _ping_result(output) is not None
    if name == "network-tc":
        return NETWORK_PASS_RE.search(output) is not None
    if name == "kernel-tc":
        return PASS_RE.search(output) is not None
    return prompt_seen


def _advance_network(
    request: RunRequest,
    process: subprocess.Popen[bytes],
    state: ProtocolState,
    *,
    prompt_seen: bool,
) -> ProtocolOutcome | None:
    network = state.network
    if network.phase < 0:
        if not prompt_seen:
            return None
        return _send_network_command(process, state, 0)

    name, _ = _network_sequence()[network.phase]
    output = bytes(network.phase_output)
    if not _phase_complete(name, output, prompt_seen):
        return None

    if name == "help":
        missing = [command.decode() for command in NETWORK_COMMANDS if re.search(rb"\b" + re.escape(command) + rb"\b", output) is None]
        if missing:
            return ProtocolOutcome("failed", "network-command-registration", None)
    elif name in ("settle", "ipv6-settle", "network-settle"):
        pass
    elif name in ("ifdown", "ifup"):
        expected = f"{name} eth0...OK".encode()
        if expected not in output:
            return ProtocolOutcome("failed", _phase_failure_reason(name), None)
    elif name == "dhcp":
        match = DHCP_RE.search(output)
        if match is not None:
            network.ipv4 = match.group(1).decode()
        if b"Failed" in output or b"get IP address fail" in output:
            if network.retry_count < 1:
                return _restart_network(process, state)
            return ProtocolOutcome("failed", "network-dhcp", None)
    elif name == "ipv6-config":
        if b"fail" in output.lower() or b"not valid" in output.lower():
            return ProtocolOutcome("failed", "network-ipv6-config", None)
    elif name == "ifconfig":
        match = IFCONFIG_IPV4_RE.search(output)
        if match is not None:
            network.ipv4 = match.group(1).decode()
        if network.ipv4 is None:
            if network.retry_count < 1:
                return _restart_network(process, state)
            return ProtocolOutcome("failed", "network-dhcp", None)
    elif name in ("gateway-ping", "gateway-ping6", "public-ping"):
        ping = _ping_result(output)
        if ping is None:
            return ProtocolOutcome("failed", "protocol-error", None)
        transmitted, received = ping
        network.pings[name] = {"transmitted": transmitted, "received": received}
        if received < 1:
            if name == "public-ping":
                # QEMU user-mode networking does not guarantee public ICMP.
                # DNS resolution is the required Internet connectivity check.
                pass
            else:
                return ProtocolOutcome("failed", _phase_failure_reason(name), None)
    elif name == "dns":
        network.dns_resolved = b"Host: example.com" in output and b" Addr: " in output
        if not network.dns_resolved or b"ERROR --" in output:
            if network.retry_count < 1:
                return _restart_network(process, state)
            return ProtocolOutcome("failed", "network-dns", None)
    elif name == "network-tc":
        match = NETWORK_PASS_RE.search(output)
        if match is None:
            return ProtocolOutcome("failed", "protocol-error", None)
        network.network_tc_pass_count = int(match.group(1))
        network.network_tc_fail_count = int(match.group(2))
        if network.network_tc_fail_count > 0 or network.network_tc_pass_count == 0:
            return ProtocolOutcome("failed", "network-tc-fail", None)
    elif name == "kernel-tc":
        return positive_terminal(request, process, state)

    return _send_network_command(process, state, network.phase + 1)


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


def negative_process_exit_outcome(
    request: RunRequest,
    state: ProtocolState,
    returncode: int | None,
) -> ProtocolOutcome | None:
    if request.expect_reject is None or request.forbid_marker is None:
        return None
    if returncode == 0 and state.required_marker_seen and not state.forbidden_marker_seen:
        return ProtocolOutcome("expected-rejection", "expected-rejection", returncode)
    return None


def initialize_artifacts(request: RunRequest, state: ProtocolState) -> None:
    request.log_path.parent.mkdir(parents=True, exist_ok=True)
    request.result_path.parent.mkdir(parents=True, exist_ok=True)
    request.log_path.write_bytes(b"")
    write_result(request, state, ProtocolOutcome("failed", "preflight", None))


def _timeout_reason(request: RunRequest, state: ProtocolState) -> str:
    if not request.validate_network or request.expect_reject is not None or state.network.phase < 0:
        return "timeout"
    name, _ = _network_sequence()[state.network.phase]
    return f"{_phase_failure_reason(name)}-timeout"


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
                    return finish(request, state, ProtocolOutcome("failed", _timeout_reason(request, state), process.poll()))
                if state.observe_deadline is not None:
                    if process.poll() is not None:
                        outcome = negative_process_exit_outcome(request, state, process.returncode)
                        if outcome is not None:
                            return finish(request, state, outcome)
                        return finish(request, state, ProtocolOutcome("failed", "qemu-exit", process.returncode))
                    if now >= state.observe_deadline:
                        return finish(request, state, ProtocolOutcome("expected-rejection", "expected-rejection", None))
                if process.stdout is None:
                    return finish(request, state, ProtocolOutcome("failed", "protocol-error", process.poll()))
                wait_seconds = min(0.1, deadline - now)
                readable, _, _ = select.select([process.stdout], [], [], wait_seconds)
                now = time.monotonic()
                if now >= deadline:
                    return finish(request, state, ProtocolOutcome("failed", _timeout_reason(request, state), process.poll()))
                if not readable:
                    if process.poll() is not None:
                        outcome = negative_process_exit_outcome(request, state, process.returncode)
                        if outcome is not None:
                            return finish(request, state, outcome)
                        return finish(request, state, ProtocolOutcome("failed", "qemu-exit", process.returncode))
                    continue
                chunk = os.read(process.stdout.fileno(), 4096)
                if not chunk:
                    if process.poll() is not None:
                        outcome = negative_process_exit_outcome(request, state, process.returncode)
                        if outcome is not None:
                            return finish(request, state, outcome)
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
                if KERNEL_ASSERT_RE.search(bytes(state.window)) is not None:
                    return finish(request, state, ProtocolOutcome("failed", "kernel-assert", None))
                negative_outcome = update_negative_markers(request, state, now)
                if negative_outcome is not None:
                    return finish(request, state, negative_outcome)
                if request.expect_reject is not None:
                    continue
                if request.validate_network:
                    state.network.phase_output.extend(chunk)
                    if len(state.network.phase_output) > PHASE_OUTPUT_BYTES:
                        del state.network.phase_output[:-PHASE_OUTPUT_BYTES]
                    prompt_seen = _prompt_count(state.network, chunk) > 0
                    network_outcome = _advance_network(
                        request,
                        process,
                        state,
                        prompt_seen=prompt_seen,
                    )
                    if network_outcome is not None:
                        return finish(request, state, network_outcome)
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
