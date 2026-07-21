#!/usr/bin/env python3
# noqa: SIZE_OK — descriptor-bound rollback fault matrix is one security state machine.
from __future__ import annotations

import json
import os
import select
import shutil
import signal
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from mem_leak_checker_qa_core import JsonValue, QaError, Writer, canonical_bytes, write_all

CONTEXT_NAME: Final = "mem-leak-checker-hardening-active-context"


@dataclass(frozen=True, slots=True)
class DirectoryIdentity:
    name: str
    device: int
    inode: int


@dataclass(frozen=True, slots=True)
class PreflightAttempt:
    source: Path
    common: Path
    baseline: str
    setup_parent: Path
    run_id: str


@dataclass(frozen=True, slots=True)
class SetupPaths:
    source: Path
    common: Path
    setup_parent: Path
    worktree: Path
    evidence: Path


@dataclass(frozen=True, slots=True)
class SetupOwnership:
    context: dict[str, JsonValue] | None
    registered: bool
    evidence_created: bool


def _open_chain(path: Path) -> tuple[int, tuple[DirectoryIdentity, ...]]:
    if not path.is_absolute():
        raise QaError("common Git directory must be absolute")
    absolute = path
    flags = os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open("/", flags)
    identities = [DirectoryIdentity("/", os.fstat(fd).st_dev, os.fstat(fd).st_ino)]
    try:
        for name in absolute.parts[1:]:
            next_fd = os.open(name, flags, dir_fd=fd)
            os.close(fd)
            fd = next_fd
            stat = os.fstat(fd)
            identities.append(DirectoryIdentity(name, stat.st_dev, stat.st_ino))
        return fd, tuple(identities)
    except OSError:
        os.close(fd)
        raise


def _identity_json(chain: tuple[DirectoryIdentity, ...]) -> list[dict[str, int | str]]:
    return [{"name": item.name, "device": item.device, "inode": item.inode} for item in chain]


def claim_context(
    common_git: Path,
    values: dict[str, str],
    fault: str | None = None,
    writer: Writer = os.write,
) -> dict[str, JsonValue]:
    directory, chain = _open_chain(common_git)
    context = {**values, "directory_chain": _identity_json(chain)}
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    fd = -1
    owned_identity: tuple[int, int] | None = None
    try:
        fd = os.open(CONTEXT_NAME, flags, 0o600, dir_fd=directory)
        claimed_stat = os.fstat(fd)
        owned_identity = (claimed_stat.st_dev, claimed_stat.st_ino)
        if fault == "context-create":
            raise QaError("injected context creation failure")
        write_all(fd, canonical_bytes(context), writer)
        if fault == "signal-wait":
            signal.pause()
        if fault == "context-fsync":
            raise QaError("injected context fsync failure")
        os.fsync(fd)
        os.close(fd)
        fd = -1
        os.fsync(directory)
        return context
    except BaseException:  # noqa: BROAD_EXCEPT_OK -- rollback must cover signals and SystemExit.
        blocked = {signal.SIGINT, signal.SIGTERM, signal.SIGHUP}
        previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK, blocked)
        if fd >= 0:
            os.close(fd)
        if owned_identity is not None:
            try:
                named = os.stat(CONTEXT_NAME, dir_fd=directory, follow_symlinks=False)
                if (named.st_dev, named.st_ino) == owned_identity:
                    os.unlink(CONTEXT_NAME, dir_fd=directory)
                    os.fsync(directory)
            except FileNotFoundError:
                owned_identity = None
        signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)
        raise
    finally:
        os.close(directory)


def read_context(common_git: Path) -> dict[str, JsonValue]:
    directory, chain = _open_chain(common_git)
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        fd = os.open(CONTEXT_NAME, flags, dir_fd=directory)
        try:
            payload = os.read(fd, 65536)
            if os.read(fd, 1):
                raise QaError("context exceeds maximum size")
        finally:
            os.close(fd)
    finally:
        os.close(directory)
    try:
        value = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise QaError("malformed context JSON") from error
    if not isinstance(value, dict):
        raise QaError("context JSON must be an object")
    if value.get("directory_chain") != _identity_json(chain):
        raise QaError("context directory identity drift")
    return value


def cleanup_context(common_git: Path, expected: dict[str, JsonValue]) -> None:
    directory, chain = _open_chain(common_git)
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        context_fd = os.open(CONTEXT_NAME, flags, dir_fd=directory)
        try:
            bound_stat = os.fstat(context_fd)
            payload = os.read(context_fd, 65536)
            if os.read(context_fd, 1):
                raise QaError("context exceeds maximum size")
            current = json.loads(payload)
        finally:
            os.close(context_fd)
        named_stat = os.stat(CONTEXT_NAME, dir_fd=directory, follow_symlinks=False)
        if (named_stat.st_dev, named_stat.st_ino) != (bound_stat.st_dev, bound_stat.st_ino):
            raise QaError("context name was replaced during cleanup")
        for key in ("run_id", "baseline_sha", "implementation_root", "evidence_dir"):
            if current.get(key) != expected.get(key):
                raise QaError(f"context ownership mismatch: {key}")
        if current.get("directory_chain") != _identity_json(chain):
            raise QaError("cleanup directory identity drift")
        os.unlink(CONTEXT_NAME, dir_fd=directory)
        os.fsync(directory)
    finally:
        os.close(directory)


def _fixture() -> tuple[Path, Path, dict[str, str]]:
    root = Path(tempfile.mkdtemp(prefix="mlc-preflight-")).resolve()
    source = root / "source"
    source.mkdir()
    subprocess.run(["git", "init", "-q"], cwd=source, check=True)
    (source / "seed").write_text("baseline\n", encoding="utf-8")
    subprocess.run(["git", "add", "seed"], cwd=source, check=True)
    commit_env = {**os.environ, "GIT_AUTHOR_NAME": "qa", "GIT_AUTHOR_EMAIL": "qa@example.invalid"}
    commit_env.update({"GIT_COMMITTER_NAME": "qa", "GIT_COMMITTER_EMAIL": "qa@example.invalid"})
    subprocess.run(["git", "commit", "-q", "-m", "baseline"], cwd=source, env=commit_env, check=True)
    baseline = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=source, text=True, capture_output=True, check=True
    ).stdout.strip()
    common = source / ".git"
    values = {
        "baseline_sha": baseline,
        "implementation_root": str(root / "setup" / "worktree"),
        "evidence_dir": str(root / "evidence"),
        "run_id": f"selftest-{os.getpid()}",
    }
    return root, common, values


def run_preflight_case(case: str) -> None:
    root, common, values = _fixture()
    unrelated = root / "unrelated"
    unrelated.write_text("retain", encoding="utf-8")
    try:
        if case in {"preflight-context-create-failure", "preflight-context-fsync-failure"}:
            fault = "context-create" if case.endswith("create-failure") else "context-fsync"
            def short_writer(fd: int, payload: bytes) -> int:
                return os.write(fd, payload[:1])
            try:
                claim_context(common, values, fault, short_writer)
            except QaError:
                rejected = True
            else:
                raise QaError(f"fault was accepted: {case}")
            if (common / CONTEXT_NAME).exists():
                raise QaError("failed claim left context residue")
        elif case == "preflight-existing-context-refusal":
            context = claim_context(common, values)
            try:
                claim_context(common, {**values, "run_id": "loser"})
            except FileExistsError:
                rejected = True
            else:
                raise QaError("existing context was replaced")
            cleanup_context(common, context)
        elif case == "preflight-concurrent-loser":
            _run_concurrent_claim_case(root, common, values)
        elif case == "preflight-ancestor-symlink":
            link = root / "common-link"
            link.symlink_to(common)
            try:
                _open_chain(link)
            except (OSError, QaError):
                rejected = True
            else:
                raise QaError("ancestor symlink was followed")
        elif case == "preflight-ancestor-replacement":
            context = claim_context(common, values)
            moved = root / "source-moved"
            source = root / "source"
            source.rename(moved)
            source.mkdir()
            (source / ".git").mkdir()
            try:
                cleanup_context(source / ".git", context)
            except (FileNotFoundError, QaError):
                rejected = True
            else:
                raise QaError("replacement ancestor passed cleanup")
            (source / ".git").rmdir()
            source.rmdir()
            moved.rename(source)
            cleanup_context(source / ".git", context)
        else:
            _run_owned_artifact_case(case, root, common, values)
        retry = root / "retry"
        retry.mkdir()
        subprocess.run(["git", "init", "-q"], cwd=retry, check=True)
        retry_values = {**values, "run_id": values["run_id"] + "-retry"}
        retry_context = claim_context(retry / ".git", retry_values)
        cleanup_context(retry / ".git", retry_context)
        if unrelated.read_text(encoding="utf-8") != "retain":
            raise QaError("preflight removed unrelated data")
    finally:
        shutil.rmtree(root)


def _run_owned_artifact_case(case: str, root: Path, common: Path, values: dict[str, str]) -> None:
    supported = {"preflight-worktree-partial", "preflight-evidence-dir-failure", "preflight-identity-failure", "preflight-owned-child-cleanup"}
    if case not in supported:
        raise QaError(f"unknown preflight self-test: {case}")
    if case == "preflight-owned-child-cleanup":
        _run_signal_cleanup_cases(common, values)
    context = claim_context(common, values)
    setup = Path(values["implementation_root"]).parent
    worktree = Path(values["implementation_root"])
    evidence = Path(values["evidence_dir"])
    setup.mkdir()
    source = common.parent
    subprocess.run(["git", "worktree", "add", "--detach", str(worktree), values["baseline_sha"]], cwd=source, stdout=subprocess.DEVNULL, check=True)
    partial_injected = False
    try:
        if case == "preflight-worktree-partial":
            try:
                raise QaError("injected failure immediately after worktree registration")
            except QaError:
                partial_injected = True
        elif case == "preflight-evidence-dir-failure":
            evidence.write_text("foreign", encoding="utf-8")
            try:
                evidence.mkdir()
            except FileExistsError:
                rejected = True
            else:
                raise QaError("evidence directory collision was accepted")
        else:
            evidence.mkdir()
        if case == "preflight-identity-failure":
            _run_identity_failure_cases(common, context)
    finally:
        blocked = {signal.SIGINT, signal.SIGTERM, signal.SIGHUP}
        previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK, blocked)
        try:
            if evidence.is_dir():
                evidence.rmdir()
            subprocess.run(
                ["git", "worktree", "remove", "--force", str(worktree)], cwd=source, check=True
            )
            cleanup_context(common, context)
        finally:
            signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)
    if evidence.is_file():
        if evidence.read_text(encoding="utf-8") != "foreign":
            raise QaError("evidence collision modified unrelated file")
        evidence.unlink()
    if case == "preflight-worktree-partial":
        if not partial_injected or worktree.exists():
            raise QaError("registered-worktree failure did not roll back exactly")
        listed = subprocess.run(
            ["git", "worktree", "list", "--porcelain"],
            cwd=source,
            text=True,
            capture_output=True,
            check=True,
        ).stdout
        if str(worktree) in listed:
            raise QaError("rolled-back worktree remains registered")
        retry_context = claim_context(common, {**values, "run_id": values["run_id"] + "-retry"})
        subprocess.run(
            ["git", "worktree", "add", "--detach", str(worktree), values["baseline_sha"]],
            cwd=source,
            stdout=subprocess.DEVNULL,
            check=True,
        )
        subprocess.run(
            ["git", "worktree", "remove", "--force", str(worktree)], cwd=source, check=True
        )
        cleanup_context(common, retry_context)
    setup.rmdir()


def _run_identity_failure_cases(common: Path, context: dict[str, JsonValue]) -> None:
    for key in ("run_id", "baseline_sha", "implementation_root", "evidence_dir"):
        bad = {**context, key: f"foreign-{key}"}
        try:
            cleanup_context(common, bad)
        except QaError:
            rejected = True
        else:
            raise QaError(f"identity mismatch cleaned owner: {key}")
        if read_context(common) != context:
            raise QaError(f"identity mismatch changed owner context: {key}")

    drifted = {**context, "directory_chain": []}
    flags = os.O_WRONLY | os.O_TRUNC | getattr(os, "O_NOFOLLOW", 0)
    directory, _ = _open_chain(common)
    try:
        fd = os.open(CONTEXT_NAME, flags, dir_fd=directory)
        try:
            write_all(fd, canonical_bytes(drifted))
            os.fsync(fd)
        finally:
            os.close(fd)
        try:
            cleanup_context(common, context)
        except QaError:
            rejected = True
        else:
            raise QaError("directory identity drift cleaned owner context")
        fd = os.open(CONTEXT_NAME, flags, dir_fd=directory)
        try:
            write_all(fd, canonical_bytes(context))
            os.fsync(fd)
        finally:
            os.close(fd)
        os.fsync(directory)
    finally:
        os.close(directory)


def _rollback_setup(
    paths: SetupPaths,
    ownership: SetupOwnership,
) -> None:
    blocked = {signal.SIGINT, signal.SIGTERM, signal.SIGHUP}
    previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK, blocked)
    try:
        if ownership.evidence_created:
            paths.evidence.rmdir()
        if ownership.registered:
            subprocess.run(
                ["git", "worktree", "remove", "--force", str(paths.worktree)],
                cwd=paths.source,
                check=True,
                stdout=subprocess.DEVNULL,
            )
        if ownership.context is not None:
            cleanup_context(paths.common, ownership.context)
        paths.setup_parent.rmdir()
    finally:
        signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)


def _complete_preflight_attempt(
    attempt: PreflightAttempt,
    writer: Writer = os.write,
) -> dict[str, JsonValue]:
    paths = SetupPaths(
        attempt.source,
        attempt.common,
        attempt.setup_parent,
        attempt.setup_parent / "worktree",
        attempt.setup_parent / "evidence",
    )
    values = {
        "baseline_sha": attempt.baseline,
        "implementation_root": str(paths.worktree),
        "evidence_dir": str(paths.evidence),
        "run_id": attempt.run_id,
    }
    context: dict[str, JsonValue] | None = None
    registered = False
    evidence_created = False
    paths.setup_parent.mkdir()
    try:
        context = claim_context(paths.common, values, writer=writer)
        subprocess.run(
            ["git", "worktree", "add", "--detach", str(paths.worktree), attempt.baseline],
            cwd=paths.source,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        registered = True
        paths.evidence.mkdir()
        evidence_created = True
        if read_context(paths.common) != context:
            raise QaError("complete preflight context identity mismatch")
        if Path(
            subprocess.run(
                ["git", "rev-parse", "--show-toplevel"],
                cwd=paths.worktree,
                check=True,
                text=True,
                capture_output=True,
            ).stdout.strip()
        ).resolve() != paths.worktree.resolve():
            raise QaError("complete preflight worktree root mismatch")
        head = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=paths.worktree,
            check=True,
            text=True,
            capture_output=True,
        ).stdout.strip()
        status = subprocess.run(
            ["git", "status", "--porcelain=v1", "--untracked-files=all"],
            cwd=paths.worktree,
            check=True,
            capture_output=True,
        ).stdout
        if head != attempt.baseline or status:
            raise QaError("complete preflight worktree identity mismatch")
        return context
    except BaseException:  # noqa: BROAD_EXCEPT_OK -- every partial setup state must roll back.
        _rollback_setup(paths, SetupOwnership(context, registered, evidence_created))
        raise


def _cleanup_complete_attempt(
    attempt: PreflightAttempt, context: dict[str, JsonValue]
) -> None:
    paths = SetupPaths(
        attempt.source,
        attempt.common,
        attempt.setup_parent,
        attempt.setup_parent / "worktree",
        attempt.setup_parent / "evidence",
    )
    _rollback_setup(
        paths,
        SetupOwnership(context, True, True),
    )


def _read_pipe_exact(fd: int, count: int, label: str) -> bytes:
    deadline = time.monotonic() + 5.0
    received = bytearray()
    while len(received) < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([fd], [], [], remaining)[0]:
            raise QaError(f"simultaneous claimant synchronization timed out: {label}")
        chunk = os.read(fd, count - len(received))
        if not chunk:
            raise QaError(f"simultaneous claimant synchronization closed early: {label}")
        received.extend(chunk)
    return bytes(received)


def _run_concurrent_claim_case(root: Path, common: Path, values: dict[str, str]) -> None:
    source = common.parent
    acquired_read, acquired_write = os.pipe()
    hold_read, hold_write = os.pipe()
    ready_read, ready_write = os.pipe()
    lost_read, lost_write = os.pipe()
    done_read, done_write = os.pipe()
    children: list[tuple[int, int, int, Path]] = []
    for index in range(2):
        gate_read, gate_write = os.pipe()
        release_read, release_write = os.pipe()
        result = root / f"claimant-{index}.json"
        setup_parent = root / f"claimant-{index}-setup"
        child_pid = os.fork()
        if child_pid == 0:
            os.close(gate_write)
            os.close(release_write)
            try:
                os.write(ready_write, b"R")
                if os.read(gate_read, 1) != b"G":
                    os._exit(3)
                held = False

                def synchronized_writer(fd: int, payload: bytes) -> int:
                    nonlocal held
                    if not held:
                        held = True
                        os.write(acquired_write, b"A")
                        if os.read(hold_read, 1) != b"H":
                            raise QaError("simultaneous claim hold was not released")
                    return os.write(fd, payload)

                run_id = f"simultaneous-{index}"
                attempt = PreflightAttempt(
                    source, common, values["baseline_sha"], setup_parent, run_id
                )
                context: dict[str, JsonValue] | None = None
                try:
                    context = _complete_preflight_attempt(
                        attempt,
                        synchronized_writer,
                    )
                except FileExistsError:
                    outcome = {
                        "outcome": "lost",
                        "run_id": run_id,
                        "setup_complete": False,
                        "setup_parent": str(setup_parent),
                    }
                    result.write_bytes(canonical_bytes(outcome))
                    os.write(lost_write, b"L")
                else:
                    outcome = {
                        "outcome": "won",
                        "run_id": run_id,
                        "setup_complete": True,
                        "setup_parent": str(setup_parent),
                    }
                    result.write_bytes(canonical_bytes(outcome))
                os.write(done_write, b"D")
                os.read(release_read, 1)
                if context is not None:
                    _cleanup_complete_attempt(attempt, context)
                os._exit(0)
            except (OSError, QaError, subprocess.SubprocessError):
                os._exit(4)
        os.close(gate_read)
        os.close(release_read)
        children.append((child_pid, gate_write, release_write, result))

    os.close(acquired_write)
    os.close(hold_read)
    os.close(ready_write)
    os.close(lost_write)
    os.close(done_write)
    if _read_pipe_exact(ready_read, 2, "ready") != b"RR":
        raise QaError("simultaneous claimants did not reach the shared gate")
    for _, gate, _, _ in children:
        os.write(gate, b"G")
        os.close(gate)
    if _read_pipe_exact(acquired_read, 1, "acquired") != b"A":
        raise QaError("simultaneous winner did not hold the exclusive claim")
    if _read_pipe_exact(lost_read, 1, "loser") != b"L":
        raise QaError("simultaneous loser did not overlap the held claim")
    os.write(hold_write, b"H")
    os.close(hold_write)
    if _read_pipe_exact(done_read, 2, "complete") != b"DD":
        raise QaError("simultaneous attempts did not publish complete outcomes")
    outcomes = [json.loads(result.read_text(encoding="utf-8")) for _, _, _, result in children]
    winners = [item for item in outcomes if item.get("outcome") == "won"]
    losers = [item for item in outcomes if item.get("outcome") == "lost"]
    if len(winners) != 1 or len(losers) != 1:
        raise QaError(f"simultaneous ownership cardinality mismatch: {outcomes}")
    if winners[0].get("setup_complete") is not True or losers[0].get("setup_complete") is not False:
        raise QaError("simultaneous setup completion boundary mismatch")
    context = read_context(common)
    if context.get("run_id") != winners[0].get("run_id"):
        raise QaError("published context does not belong to simultaneous winner")
    winner_parent = Path(str(winners[0]["setup_parent"]))
    loser_parent = Path(str(losers[0]["setup_parent"]))
    if not (winner_parent / "worktree").is_dir() or not (winner_parent / "evidence").is_dir():
        raise QaError("simultaneous winner did not retain complete setup resources")
    if loser_parent.exists():
        raise QaError("simultaneous loser left setup-parent residue")
    listed = subprocess.run(
        ["git", "worktree", "list", "--porcelain"],
        cwd=source,
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    if str(winner_parent / "worktree") not in listed or str(loser_parent / "worktree") in listed:
        raise QaError("simultaneous worktree ownership mismatch")
    for _, _, release, _ in children:
        os.write(release, b"R")
        os.close(release)
    statuses = [os.waitpid(child_pid, 0) for child_pid, _, _, _ in children]
    if any(not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0 for _, status in statuses):
        raise QaError("simultaneous claimant did not exit cleanly")
    if (common / CONTEXT_NAME).exists():
        raise QaError("simultaneous winner cleanup left context residue")
    if winner_parent.exists() or loser_parent.exists():
        raise QaError("simultaneous cleanup left setup-parent residue")
    retry_attempt = PreflightAttempt(
        source,
        common,
        values["baseline_sha"],
        winner_parent,
        str(winners[0]["run_id"]) + "-retry",
    )
    retry_context = _complete_preflight_attempt(retry_attempt)
    _cleanup_complete_attempt(retry_attempt, retry_context)


def _run_signal_cleanup_cases(common: Path, values: dict[str, str]) -> None:
    for current_signal in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        child_pid = os.fork()
        if child_pid == 0:
            try:
                for handled in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
                    signal.signal(handled, lambda _number, _frame: (_ for _ in ()).throw(KeyboardInterrupt()))
                claim_context(common, {**values, "run_id": f"signal-{current_signal}"}, "signal-wait")
            except KeyboardInterrupt:
                os._exit(0)
            os._exit(2)
        deadline = time.monotonic() + 5
        while not (common / CONTEXT_NAME).exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        if not (common / CONTEXT_NAME).exists():
            os.kill(child_pid, signal.SIGKILL)
            os.waitpid(child_pid, 0)
            raise QaError("signal fixture did not claim context")
        os.kill(child_pid, current_signal)
        try:
            os.kill(child_pid, current_signal)
        except ProcessLookupError:
            waited_pid, status = os.waitpid(child_pid, 0)
        else:
            waited_pid, status = os.waitpid(child_pid, 0)
        if waited_pid != child_pid or not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
            raise QaError("signal cleanup child did not terminate cleanly")
        if (common / CONTEXT_NAME).exists():
            raise QaError("signal cleanup left owned context")
