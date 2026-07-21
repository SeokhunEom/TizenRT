# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# ─── How to run ───
# uv run tools/mem_leak_checker_task4_authoritative.py <root> <evidence-dir> <receipt> <sha>
from __future__ import annotations

import fcntl
import hashlib
import json
import os
import stat
import subprocess
import sys
from pathlib import Path
from typing import Final

from mem_leak_checker_qa_core import JsonValue, QaError
from mem_leak_checker_task4_evidence import SCENARIO, read_json, verify_receipt
from mem_leak_checker_task4_scenarios import execute_entry

CONTENT_PATHS: Final = (
    "os/kernel/debug/mem_leak_checker_graph.c",
    "os/kernel/debug/mem_leak_checker_graph.h",
    "os/kernel/debug/mem_leak_checker_graph_internal.h",
    "os/kernel/debug/mem_leak_checker_graph_validate.c",
    "os/kernel/debug/tests/test_mem_leak_checker_graph.c",
    "os/kernel/debug/tests/test_mem_leak_checker_graph_depth.c",
    "tools/mem_leak_checker_qa.sh",
    "tools/mem_leak_checker_qa_core.py",
    "tools/mem_leak_checker_scenarios/task-4.json",
    "tools/mem_leak_checker_task4_authoritative.py",
    "tools/mem_leak_checker_task4_evidence.py",
    "tools/mem_leak_checker_task4_qa.sh",
    "tools/mem_leak_checker_task4_scenarios.py",
    "tools/test_mem_leak_checker_graph.sh",
)
PUBLICATION: Final[dict[str, JsonValue]] = {
    "atomic_visibility": False,
    "concurrency_scope": "cooperating_sealers_only",
    "cryptographic_authentication": False,
    "directory_fsync": True,
    "external_writer_authenticated": False,
    "external_writer_mutation_scope": "detected_where_observed_otherwise_outside_receipt_trust_boundary",
    "file_fsync": True,
    "idempotent_replay_fsync": True,
    "immutable": False,
    "mac_authenticated": False,
    "mode": "exclusive_final_inode_weaker_exfat",
    "reopened_named_payload_fsync": True,
    "surviving_identity_validation": "original_descriptor_or_reopened_exact_named_payload",
}


def git_blob(root: Path, receiving_sha: str, relative: str) -> bytes:
    return subprocess.run(
        ["git", "-C", str(root), "show", f"{receiving_sha}:{relative}"],
        check=True,
        capture_output=True,
    ).stdout


def content_digest(root: Path, receiving_sha: str, plan_sha: str) -> str:
    digest = hashlib.sha256()
    for relative in CONTENT_PATHS:
        digest.update(relative.encode() + b"\0" + git_blob(root, receiving_sha, relative))
    digest.update(
        b"publication\0" + json.dumps(PUBLICATION, sort_keys=True, separators=(",", ":")).encode()
    )
    digest.update(b"normalized_plan_sha256\0" + plan_sha.encode())
    return digest.hexdigest()


def scenario_commands(scenario: dict[str, JsonValue]) -> dict[str, JsonValue]:
    commands: dict[str, JsonValue] = {}
    for bucket in ("red", "happy", "failure"):
        entries = scenario.get(bucket)
        if not isinstance(entries, list):
            raise QaError(f"task-4 {bucket} scenarios malformed")
        selected: list[JsonValue] = []
        for entry in entries:
            if not isinstance(entry, dict) or not isinstance(entry.get("command"), str):
                raise QaError(f"task-4 {bucket} command malformed")
            selected.append(entry["command"])
        commands[bucket] = selected
    return commands


def run_declared(root: Path, evidence: Path, scenario: dict[str, JsonValue]) -> list[JsonValue]:
    exits: list[JsonValue] = []
    red_entries = scenario.get("red")
    if not isinstance(red_entries, list) or len(red_entries) != 1:
        raise QaError("task-4 RED scenario cardinality mismatch")
    red = red_entries[0]
    if not isinstance(red, dict) or red.get("expected_exit") != 86:
        raise QaError("task-4 RED expected exit mismatch")
    command = red.get("command")
    if not isinstance(command, str):
        raise QaError("task-4 RED command malformed")
    verify_receipt(root, evidence)
    exits.append({"command": command, "exit": 86})
    for bucket in ("happy", "failure"):
        entries = scenario.get(bucket)
        if not isinstance(entries, list):
            raise QaError(f"task-4 {bucket} scenarios malformed")
        for entry in entries:
            executed, _ = execute_entry(root, evidence, entry, bucket)
            expected = entry.get("expected_exit") if isinstance(entry, dict) else None
            exits.append({"command": executed, "exit": 0 if expected is None else expected})
    return exits


def require_identical(descriptor: int, payload: bytes) -> None:
    if not stat.S_ISREG(os.fstat(descriptor).st_mode):
        raise QaError("task-4 receipt winner is not regular")
    chunks: list[bytes] = []
    while chunk := os.read(descriptor, 65536):
        chunks.append(chunk)
    if b"".join(chunks) != payload:
        raise QaError("task-4 idempotent receipt drift")


def sync_identical_survivor(
    root: Path,
    directory: int,
    descriptor: int,
    payload: bytes,
) -> None:
    require_identical(descriptor, payload)
    if os.environ.get("MLC_TASK4_IDENTICAL_FSYNC_FAILURE") == "1":
        if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
            raise QaError("task-4 identical-fsync fault escaped its private fixture")
        raise OSError("injected task-4 identical receipt fsync failure")
    os.fsync(descriptor)
    os.fsync(directory)
    marker_value = os.environ.get("MLC_TASK4_SURVIVOR_FSYNC_MARKER")
    if marker_value is not None:
        marker = Path(marker_value).resolve(strict=False)
        if marker.parent != root.parent or not str(root).startswith(
            ("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")
        ):
            raise QaError("task-4 survivor marker escaped its private fixture")
        marker.write_text("reopened_named_file_and_directory_fsynced\n")


def cleanup_created_receipt(
    root: Path,
    directory: int,
    name: str,
    descriptor: int,
    owned: os.stat_result | None,
    owned_name_identity: tuple[int, int] | None,
) -> None:
    identity = (owned.st_dev, owned.st_ino) if owned is not None else None
    if identity is None:
        try:
            if os.environ.get("MLC_TASK4_CLEANUP_FSTAT_FAILURE") == "1":
                if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                    raise QaError("task-4 cleanup-fstat fault escaped its private fixture")
                raise OSError("injected task-4 cleanup fstat failure")
            descriptor_identity = os.fstat(descriptor)
            identity = (descriptor_identity.st_dev, descriptor_identity.st_ino)
        except OSError:
            identity = owned_name_identity
    if identity is None:
        return
    try:
        named = os.stat(name, dir_fd=directory, follow_symlinks=False)
    except FileNotFoundError:
        return
    if (named.st_dev, named.st_ino) != identity:
        return
    os.unlink(name, dir_fd=directory)
    os.fsync(directory)
    marker_value = os.environ.get("MLC_TASK4_CLEANUP_MARKER")
    if marker_value is not None:
        marker = Path(marker_value).resolve(strict=False)
        if marker.parent != root.parent or not str(root).startswith(
            ("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")
        ):
            raise QaError("task-4 cleanup marker escaped its private fixture")
        marker.write_text("owned_receipt_unlinked_directory_fsynced\n")


def publish(root: Path, path: Path, payload: bytes, plan_sha: str, lifecycle_directory: int) -> None:
    if not path.is_absolute() or root not in path.parents:
        raise QaError("task-4 receipt path escapes the receiving worktree")
    directory = os.dup(lifecycle_directory)
    try:
        locked = os.fstat(directory)
        named_directory = os.stat(path.parent, follow_symlinks=False)
        if not stat.S_ISDIR(locked.st_mode) or (locked.st_dev, locked.st_ino) != (
            named_directory.st_dev,
            named_directory.st_ino,
        ):
            raise QaError("task-4 lifecycle lock directory changed before publication")
        validation_task = os.environ.get("MLC_AUTHORITATIVE_TASK", "4")
        validation = subprocess.run(
            [
                "bash",
                str(root / "tools/mem_leak_checker_task5_scenarios.sh"),
                "validate-receiving",
                "--receipt",
                str(path.relative_to(root)),
                "--task",
                validation_task,
            ],
            cwd=root,
            check=True,
            capture_output=True,
            text=True,
        )
        if validation.stdout.strip() != plan_sha:
            raise QaError("task-4 authenticated plan changed before publication")
        try:
            descriptor = os.open(
                path.name,
                os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0),
                dir_fd=directory,
            )
        except FileNotFoundError:
            owned: os.stat_result | None = None
            owned_name_identity: tuple[int, int] | None = None
            try:
                descriptor = os.open(
                    path.name,
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
                    0o600,
                    dir_fd=directory,
                )
            except FileExistsError:
                descriptor = os.open(
                    path.name,
                    os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0),
                    dir_fd=directory,
                )
                try:
                    sync_identical_survivor(root, directory, descriptor, payload)
                finally:
                    os.close(descriptor)
                return
            try:
                if os.environ.get("MLC_TASK4_NAME_STAT_FAILURE") == "1":
                    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                        raise QaError("task-4 named-stat fault escaped its private fixture")
                    raise OSError("injected task-4 named-stat failure")
                created_name = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
                if not stat.S_ISREG(created_name.st_mode):
                    raise QaError("task-4 created receipt is not regular")
                owned_name_identity = (created_name.st_dev, created_name.st_ino)
                if os.environ.get("MLC_TASK4_PRE_FSTAT_FOREIGN_SUBSTITUTION") == "1":
                    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                        raise QaError("task-4 foreign substitution escaped its private fixture")
                    os.unlink(path.name, dir_fd=directory)
                    foreign = os.open(
                        path.name,
                        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
                        0o600,
                        dir_fd=directory,
                    )
                    try:
                        os.write(foreign, b"foreign task-4 receipt\n")
                        os.fsync(foreign)
                    finally:
                        os.close(foreign)
                    os.fsync(directory)
                    raise OSError("injected task-4 foreign substitution")
                if os.environ.get("MLC_TASK4_PRE_FSTAT_FAILURE") == "1":
                    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                        raise QaError("task-4 pre-fstat fault escaped its private fixture")
                    raise OSError("injected task-4 pre-fstat failure")
                owned = os.fstat(descriptor)
                offset = 0
                while offset < len(payload):
                    written = os.write(descriptor, payload[offset:])
                    if written <= 0:
                        raise QaError("task-4 receipt write made no progress")
                    offset += written
                os.fsync(descriptor)
                if os.environ.get("MLC_TASK4_IDENTICAL_SUBSTITUTION") == "1":
                    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                        raise QaError("task-4 identical substitution escaped its private fixture")
                    os.unlink(path.name, dir_fd=directory)
                    survivor = os.open(
                        path.name,
                        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
                        0o600,
                        dir_fd=directory,
                    )
                    try:
                        offset = 0
                        while offset < len(payload):
                            written = os.write(survivor, payload[offset:])
                            if written <= 0:
                                raise QaError("task-4 survivor write made no progress")
                            offset += written
                    finally:
                        os.close(survivor)
                if os.environ.get("MLC_TASK4_AFTER_WRITE_MUTATION") == "1":
                    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                        raise QaError("task-4 write mutation escaped its private fixture")
                    with (root / "os/kernel/debug/mem_leak_checker_graph.c").open("ab") as stream:
                        stream.write(b"task-4 after-write mutation\n")
                subprocess.run(
                    [
                        "bash",
                        str(root / "tools/mem_leak_checker_task5_scenarios.sh"),
                        "validate-receiving",
                        "--receipt",
                        str(path.relative_to(root)),
                        "--task",
                        validation_task,
                    ],
                    cwd=root,
                    check=True,
                    capture_output=True,
                )
                named_receipt = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
                if (named_receipt.st_dev, named_receipt.st_ino) != (owned.st_dev, owned.st_ino):
                    survivor = os.open(
                        path.name,
                        os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0),
                        dir_fd=directory,
                    )
                    try:
                        sync_identical_survivor(root, directory, survivor, payload)
                    finally:
                        os.close(survivor)
                else:
                    os.fsync(directory)
            except BaseException as error:
                try:
                    cleanup_created_receipt(
                        root,
                        directory,
                        path.name,
                        descriptor,
                        owned,
                        owned_name_identity,
                    )
                except BaseException as cleanup_error:
                    error.add_note(f"task-4 receipt cleanup also failed: {cleanup_error}")
                raise
            finally:
                os.close(descriptor)
        else:
            try:
                sync_identical_survivor(root, directory, descriptor, payload)
            finally:
                os.close(descriptor)
    finally:
        os.close(directory)


def seal_locked(root: Path, evidence: Path, receipt: Path, receiving_sha: str, directory: int) -> str:
    scenario_blob = git_blob(root, receiving_sha, SCENARIO)
    scenario = read_json(root / SCENARIO)
    exits = run_declared(root, evidence, scenario)
    for arguments in (("diff", "--quiet", "--"), ("diff", "--cached", "--quiet", "--")):
        if subprocess.run(
            ["git", "-C", str(root), *arguments],
            check=False,
            capture_output=True,
        ).returncode != 0:
            raise QaError("task-4 tracked receiving state changed during scenarios")
    current_sha = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if current_sha != receiving_sha:
        raise QaError("task-4 HEAD changed during scenarios")
    validation = subprocess.run(
        [
            "bash",
            str(root / "tools/mem_leak_checker_task5_scenarios.sh"),
            "validate-receiving",
            "--receipt",
            str(receipt.relative_to(root)),
            "--task",
            "4",
        ],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    plan_sha = validation.stdout.strip()
    if len(plan_sha) != 64 or any(character not in "0123456789abcdef" for character in plan_sha):
        raise QaError("task-4 authenticated plan digest malformed")
    receiving_tree = subprocess.run(
        ["git", "-C", str(root), "rev-parse", f"{receiving_sha}^{{tree}}"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    value: dict[str, JsonValue] = {
        "content_sha256": content_digest(root, receiving_sha, plan_sha),
        "normalized_plan_sha256": plan_sha,
        "publication": PUBLICATION,
        "qemu": "deferred_unexecuted_baseline_link_failure",
        "receiving_sha": receiving_sha,
        "receiving_tree": receiving_tree,
        "scenario_commands": scenario_commands(scenario),
        "scenario_exits": exits,
        "scenario_sha256": hashlib.sha256(scenario_blob).hexdigest(),
        "schema": 2,
        "status": "host_scenarios_sealed_qemu_explicitly_deferred",
        "task": 4,
    }
    payload = (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()
    publish(root, receipt, payload, plan_sha, directory)
    return hashlib.sha256(payload).hexdigest()


def seal(root: Path, evidence: Path, receipt: Path, receiving_sha: str) -> str:
    if not receipt.is_absolute() or root not in receipt.parents:
        raise QaError("task-4 receipt path escapes the receiving worktree")
    relative = receipt.relative_to(root)
    inherited = os.environ.get("MLC_TASK4_LOCK_FD")
    if inherited is not None:
        directory = os.dup(int(inherited))
        locked = os.fstat(directory)
        named = os.stat(receipt.parent, follow_symlinks=False)
        if not stat.S_ISDIR(locked.st_mode) or (locked.st_dev, locked.st_ino) != (
            named.st_dev,
            named.st_ino,
        ):
            os.close(directory)
            raise QaError("task-4 lifecycle lock directory identity mismatch")
        fcntl.flock(directory, fcntl.LOCK_EX)
        try:
            return seal_locked(root, evidence, receipt, receiving_sha, directory)
        finally:
            os.close(directory)
    directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
    try:
        for component in relative.parent.parts:
            child = os.open(
                component,
                os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
                dir_fd=directory,
            )
            os.close(directory)
            directory = child
        fcntl.flock(directory, fcntl.LOCK_EX)
        return seal_locked(root, evidence, receipt, receiving_sha, directory)
    finally:
        os.close(directory)


def main() -> int:
    if len(sys.argv) != 5:
        return 64
    root = Path(sys.argv[1]).resolve(strict=True)
    evidence = Path(sys.argv[2]).resolve(strict=True)
    receipt = Path(sys.argv[3]).resolve(strict=False)
    try:
        print(seal(root, evidence, receipt, sys.argv[4]))
    except (QaError, OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"task-4 authoritative seal failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
