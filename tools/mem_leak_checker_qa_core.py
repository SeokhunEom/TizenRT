#!/usr/bin/env python3
# noqa: SIZE_OK — QEMU session resource and publication primitives share one boundary.
from __future__ import annotations

import errno
import hashlib
import json
import os
import re
import selectors
import signal
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Final, Protocol, TypeAlias

PROMPT: Final = "TASH>>"
TERMINAL_FIXTURES: Final = {
    "mlc_bootstrap": re.compile(
        r"^MLC_QA fixture=mlc_bootstrap status=PASS baseline_sha=[0-9a-f]{40}$", re.M
    ),
    "mlc_characterization": re.compile(
        r"^MLC_QA fixture=mlc_characterization self=(hidden|reported|unobserved) "
        r"cycle=(hidden|reported|unobserved) chain_only_head=(reported|other|unobserved) "
        r"gating=false$",
        re.M,
    ),
}


class QaError(RuntimeError):
    pass


JsonValue: TypeAlias = str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]


class Writer(Protocol):
    def __call__(self, fd: int, payload: bytes) -> int: ...


@dataclass(frozen=True, slots=True)
class SessionResult:
    transcript: str
    records: tuple[str, ...]


def canonical_bytes(value: JsonValue) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256_path(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_all(fd: int, payload: bytes, writer: Writer = os.write) -> None:
    offset = 0
    while offset < len(payload):
        try:
            written = writer(fd, payload[offset:])
        except InterruptedError:
            continue
        if written <= 0:
            raise QaError("write made no progress")
        offset += written


def _open_directory(path: Path) -> int:
    return os.open(path, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))


def _supports_link_publication(parent: Path) -> bool:
    probe = parent / f".mlc-link-probe-{os.getpid()}-{time.monotonic_ns()}"
    linked = probe.with_suffix(".linked")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(probe, flags, 0o600)
    os.close(fd)
    try:
        try:
            os.link(probe, linked, follow_symlinks=False)
        except OSError as error:
            if error.errno in {errno.ENOTSUP, errno.EOPNOTSUPP, errno.EXDEV, errno.EPERM}:
                return False
            raise
        return True
    finally:
        linked.unlink(missing_ok=True)
        probe.unlink(missing_ok=True)


def publication_record(parent: Path, force_weak: bool = False) -> dict[str, JsonValue]:
    strong = not force_weak and _supports_link_publication(parent)
    if strong:
        return {
            "mode": "linkat_noreplace_staged",
            "atomic_visibility": True,
            "immutable": False,
            "file_fsync": True,
            "directory_fsync": True,
        }
    return {
        "mode": "exclusive_final_inode_weaker_exfat",
        "atomic_visibility": False,
        "immutable": False,
        "file_fsync": True,
        "directory_fsync": True,
    }


def publish_json(
    path: Path,
    value: JsonValue,
    *,
    writer: Writer = os.write,
    force_weak: bool = False,
) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    publication = publication_record(path.parent, force_weak)
    if isinstance(value, dict):
        value = {**value, "publication": publication}
    payload = canonical_bytes(value)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    directory = _open_directory(path.parent)
    try:
        if publication["mode"] == "exclusive_final_inode_weaker_exfat":
            fd = os.open(path, flags, 0o600)
            opened = os.fstat(fd)
            owned_identity = (opened.st_dev, opened.st_ino)
            try:
                write_all(fd, payload, writer)
                os.fsync(fd)
            except BaseException:  # noqa: BROAD_EXCEPT_OK -- publication cleanup covers signals.
                try:
                    named = None
                    try:
                        named = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
                    except FileNotFoundError:
                        named = None
                    if named is not None and (named.st_dev, named.st_ino) == owned_identity:
                        os.unlink(path.name, dir_fd=directory)
                        os.fsync(directory)
                finally:
                    os.close(fd)
                raise
            os.close(fd)
            os.fsync(directory)
            return

        staging = path.with_name(f".{path.name}.{os.getpid()}.{time.monotonic_ns()}.tmp")
        fd = os.open(staging, flags, 0o600)
        try:
            write_all(fd, payload, writer)
            os.fsync(fd)
        except BaseException:  # noqa: BROAD_EXCEPT_OK -- staging cleanup covers signals.
            os.close(fd)
            staging.unlink(missing_ok=True)
            os.fsync(directory)
            raise
        os.close(fd)
        try:
            os.link(staging, path, follow_symlinks=False)
            os.fsync(directory)
        except BaseException:  # noqa: BROAD_EXCEPT_OK -- collision cleanup covers signals.
            staging.unlink(missing_ok=True)
            os.fsync(directory)
            raise
        staging.unlink()
        os.fsync(directory)
    finally:
        os.close(directory)


def parse_completed_transcript(transcript: str, fixtures: tuple[str, ...]) -> SessionResult:
    cleaned = re.sub(r"TASH>>\x00+", PROMPT, transcript)
    terminal_end = 0
    records: list[str] = []
    for fixture in fixtures:
        pattern = TERMINAL_FIXTURES.get(fixture)
        if pattern is None:
            raise QaError(f"unknown fixture: {fixture}")
        match = pattern.search(cleaned)
        if match is None:
            raise QaError(f"missing terminal record: {fixture}")
        terminal_end = max(terminal_end, match.end())
        records.append(match.group(0))
    summary = re.search(
        r"^########## Kernel TC End \[PASS : [0-9]+, FAIL : 0\] ##########$", cleaned, re.M
    )
    if summary is None or summary.start() < terminal_end:
        raise QaError("missing successful terminal summary")
    if "MLC_INCOMPLETE" in cleaned and ("LEAK   |" in cleaned or "NO MEMORY LEAK" in cleaned):
        raise QaError("verdict emitted after incomplete snapshot")
    if cleaned.find(PROMPT, summary.end()) < 0:
        raise QaError("missing fresh prompt after successful terminal summary")
    return SessionResult(cleaned, tuple(records))


def _process_group_exists(pgid: int) -> bool:
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        return False
    except PermissionError as error:
        raise QaError(f"refusing inaccessible process group: {pgid}") from error
    return True


def _wait_for_process_group_exit(pgid: int, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while _process_group_exists(pgid) and time.monotonic() < deadline:
        time.sleep(min(0.01, max(0.0, deadline - time.monotonic())))
    return not _process_group_exists(pgid)


def terminate_process_group(
    process: subprocess.Popen[bytes], pgid: int, grace: float = 1.0
) -> None:
    if pgid <= 1 or pgid != process.pid or pgid == os.getpgrp():
        raise QaError(f"refusing unsafe process group: {pgid}")
    if _process_group_exists(pgid):
        try:
            os.killpg(pgid, signal.SIGTERM)
        except ProcessLookupError:
            term_sent = False
        else:
            term_sent = True
        if not _wait_for_process_group_exit(pgid, grace):
            try:
                os.killpg(pgid, signal.SIGKILL)
            except ProcessLookupError:
                kill_sent = False
            else:
                kill_sent = True
            if not _wait_for_process_group_exit(pgid, grace):
                raise QaError(
                    f"process group survived cleanup: {pgid} term={term_sent} kill={kill_sent}"
                )
    try:
        process.wait(timeout=grace)
    except subprocess.TimeoutExpired as error:
        raise QaError(f"process-group leader could not be reaped: {process.pid}") from error


def run_qemu(binary: Path, fixtures: tuple[str, ...], timeout: float) -> SessionResult:
    command = ["qemu-system-arm", "-M", "lm3s6965evb", "-kernel", str(binary), "-nographic"]
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    pgid = process.pid
    if process.stdin is None or process.stdout is None:
        terminate_process_group(process, pgid)
        raise QaError("failed to create QEMU pipes")
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout
    transcript = bytearray()
    sent = False
    command_offset = 0
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(timeout=min(0.2, max(0.0, deadline - time.monotonic()))):
                chunk = os.read(key.fd, 4096)
                if not chunk:
                    raise QaError(f"QEMU exited before completion: {process.poll()}")
                transcript.extend(chunk)
            text = transcript.decode("utf-8", errors="replace")
            normalized = re.sub(r"TASH>>\x00+", PROMPT, text)
            if not sent and PROMPT in normalized:
                process.stdin.write(b"kernel_tc\n")
                process.stdin.flush()
                sent = True
                command_offset = len(text)
            if sent:
                try:
                    return parse_completed_transcript(text[command_offset:], fixtures)
                except QaError:
                    incomplete_transcript = True
        raise QaError("QEMU session timed out")
    finally:
        selector.close()
        terminate_process_group(process, pgid)


def validate_root(root: Path) -> str:
    required = (root / "os" / "Makefile", root / "os" / "tools" / "configure.sh", root / "apps")
    if not root.is_absolute() or any(not path.exists() for path in required):
        raise QaError("missing execution-root paths")
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"], cwd=root, text=True, capture_output=True, check=True
    )
    if Path(result.stdout.strip()).resolve() != root.resolve():
        raise QaError("execution root is not the repository root")
    return subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True, capture_output=True, check=True
    ).stdout.strip()


def run_checked(command: list[str], cwd: Path, log: Path, timeout: float = 300.0) -> None:
    log.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    with log.open("xb") as stream:
        process = subprocess.Popen(
            command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True
        )
        pgid = process.pid
        try:
            return_code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            terminate_process_group(process, pgid)
            raise QaError(f"pipeline command timed out: {' '.join(command)}") from error
        except BaseException:  # noqa: BROAD_EXCEPT_OK -- owned process groups must not outlive interrupts.
            terminate_process_group(process, pgid)
            raise
    if return_code != 0:
        raise QaError(f"pipeline command failed ({return_code}): {' '.join(command)}")
