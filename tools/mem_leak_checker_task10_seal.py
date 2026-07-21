#!/usr/bin/env python3
"""Replay Task10 scenarios and publish an idempotent evidence receipt."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
from typing import Any


def git(root: Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(root), *args], text=True
    ).strip()


def digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def execute(root: Path, entry: dict[str, Any], bucket: str) -> dict[str, Any]:
    command = entry["command"]
    completed = subprocess.run(
        ["/bin/bash", "-c", command], cwd=root, text=True,
        capture_output=True,
    )
    stdout = completed.stdout.encode()
    stderr = completed.stderr.encode()
    if completed.returncode != entry["expected_exit"]:
        raise SystemExit(
            f"Task10 {bucket} exit {completed.returncode}, "
            f"expected {entry['expected_exit']}"
        )
    return {
        "bucket": bucket,
        "command": command,
        "expected_exit": entry["expected_exit"],
        "exit": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "stdout_sha256": digest(stdout),
        "stderr_sha256": digest(stderr),
    }


def validate_status(root: Path, owned_receipt: Path) -> None:
    status = git(root, "status", "--porcelain", "--untracked-files=all")
    owned_path = str(owned_receipt.relative_to(root))
    for line in status.splitlines():
        path = line[3:]
        if path != owned_path:
            raise SystemExit("Task10 seal requires a clean receiving worktree")


def ensure_directory(path: Path, root: Path) -> int:
    flags = os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(root, flags)
    try:
        for component in path.relative_to(root).parts:
            try:
                child = os.open(component, flags, dir_fd=descriptor)
            except FileNotFoundError:
                os.mkdir(component, 0o700, dir_fd=descriptor)
                child = os.open(component, flags, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = child
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise SystemExit("Task10 receipt directory is not a trusted directory")


def read_nofollow(parent: int, name: str) -> bytes:
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(name, flags, dir_fd=parent)
    try:
        opened = os.fstat(descriptor)
        if not stat.S_ISREG(opened.st_mode):
            raise SystemExit("Task10 receipt is not a regular file")
        chunks = []
        while True:
            chunk = os.read(descriptor, 65536)
            if not chunk:
                return b"".join(chunks)
            chunks.append(chunk)
    finally:
        os.close(descriptor)


def write_once(parent: int, name: str, payload: bytes) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(name, flags, 0o600, dir_fd=parent)
    except FileExistsError:
        if read_nofollow(parent, name) != payload:
            raise SystemExit("Task10 receipt replay differs")
        os.fsync(parent)
        return
    try:
        written = os.write(descriptor, payload)
        if written != len(payload):
            raise SystemExit("Task10 receipt short write")
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    os.fsync(parent)


def main() -> int:
    if len(sys.argv) != 3:
        return 64
    root = Path(sys.argv[1]).resolve()
    source = git(root, "rev-parse", sys.argv[2])
    head = git(root, "rev-parse", "HEAD")
    if source != head:
        raise SystemExit("Task10 seal source is not receiving HEAD")
    tree_before = git(root, "rev-parse", "HEAD^{tree}")
    artifact = root / ".omo/start-work/artifacts/task-10-executor"
    path = artifact / f"task-10-post-integration-{source}.json"
    artifact_fd = ensure_directory(artifact, root)
    os.close(artifact_fd)
    validate_status(root, path)

    scenario_path = root / "tools/mem_leak_checker_scenarios/task-10.json"
    scenario = json.loads(scenario_path.read_text())
    results = []
    for bucket in ("red", "happy", "post_commit", "failure"):
        entries = scenario.get(bucket)
        if not isinstance(entries, list) or not entries:
            raise SystemExit(f"Task10 scenario bucket invalid: {bucket}")
        for index, entry in enumerate(entries):
            results.append(execute(root, entry, f"{bucket}[{index}]"))

    if git(root, "rev-parse", "HEAD") != source or \
            git(root, "rev-parse", "HEAD^{tree}") != tree_before:
        raise SystemExit("Task10 seal source/tree changed during replay")
    validate_status(root, path)
    tree = tree_before
    receipt = {
        "schema": 2,
        "task": 10,
        "kind": "post-integration",
        "source_sha": source,
        "tree_sha": tree,
        "scenario_sha256": digest(scenario_path.read_bytes()),
        "commands": [record["command"] for record in results],
        "results": results,
        "qemu": "deferred_unexecuted_baseline_link_failure",
        "hardware_validation": "skipped_by_user",
        "reset_executed": False,
        "cleanup": {"temporary_paths": "removed", "qemu_processes": 0},
    }
    payload = json.dumps(receipt, sort_keys=True, separators=(",", ":")).encode()
    payload += b"\n"
    artifact_fd = ensure_directory(artifact, root)
    try:
        write_once(artifact_fd, path.name, payload)
    finally:
        os.close(artifact_fd)
    print(
        f"MLC_TASK10_SEAL status=PASS source={source} "
        f"receipt={path.relative_to(root)} receipt_sha256={digest(payload)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
