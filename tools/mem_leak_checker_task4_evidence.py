# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# ─── How to run ───
# uv run tools/mem_leak_checker_task4_evidence.py create|verify <root> <evidence-dir>
from __future__ import annotations

import hashlib
import json
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Final, assert_never

from mem_leak_checker_qa_core import JsonValue, QaError, publish_json, sha256_path

BASELINE: Final = "37829f7e4b8cbb3948f1451d5c693be857c551f6"
SCENARIO: Final = "tools/mem_leak_checker_scenarios/task-4.json"
RED_COMMAND: Final = (
    "tools/mem_leak_checker_qa.sh red --task 4 --config qemu/tc_1m "
    "--fixtures mlc_graph_core,mlc_zero_graph"
)
FIXTURE_PATHS: Final = (
    "os/kernel/debug/tests/test_mem_leak_checker_graph.c",
    "os/kernel/debug/tests/test_mem_leak_checker_graph_depth.c",
    "tools/mem_leak_checker_qa.sh",
    "tools/mem_leak_checker_scenarios/task-4.json",
    "tools/mem_leak_checker_task4_authoritative.py",
    "tools/mem_leak_checker_task4_evidence.py",
    "tools/mem_leak_checker_task4_qa.sh",
    "tools/mem_leak_checker_task4_scenarios.py",
    "tools/test_mem_leak_checker_graph.sh",
)
EXPECTED_COMMANDS: Final = {
    "red": (RED_COMMAND,),
    "happy": (
        "tools/test_mem_leak_checker_graph.sh --fixtures mlc_graph_core,mlc_zero_graph --repeat 50",
        "tools/mem_leak_checker_qa.sh qemu --task 4 --fixtures mlc_graph_core,mlc_zero_graph --repeat 50",
    ),
    "failure": (
        "tools/test_mem_leak_checker_graph.sh --fixtures mlc_frontier_tarjan_exhaustion,mlc_tarjan_max_depth",
        "tools/mem_leak_checker_qa.sh qemu --task 4 --fixtures mlc_frontier_tarjan_exhaustion,mlc_tarjan_max_depth",
        "tools/mem_leak_checker_task4_qa.sh negative",
    ),
}


def git(root: Path, *arguments: str, env: dict[str, str] | None = None) -> str:
    completed = subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=True,
        capture_output=True,
        text=True,
        env=env,
    )
    return completed.stdout.strip()


def git_bytes(root: Path, *arguments: str) -> bytes:
    completed = subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=True,
        capture_output=True,
    )
    return completed.stdout


def read_regular(path: Path) -> bytes:
    absolute = path
    if not absolute.is_absolute():
        raise QaError(f"absolute file path required: {path}")
    directory = os.open("/", os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
    try:
        for component in absolute.parent.parts[1:]:
            child = os.open(
                component,
                os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
                dir_fd=directory,
            )
            os.close(directory)
            directory = child
        descriptor = os.open(
            absolute.name,
            os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0),
            dir_fd=directory,
        )
        try:
            if not stat.S_ISREG(os.fstat(descriptor).st_mode):
                raise QaError(f"regular file required: {path}")
            chunks: list[bytes] = []
            while chunk := os.read(descriptor, 65536):
                chunks.append(chunk)
            return b"".join(chunks)
        finally:
            os.close(descriptor)
    finally:
        os.close(directory)


def read_json(path: Path) -> dict[str, JsonValue]:
    value = json.loads(read_regular(path))
    if not isinstance(value, dict):
        raise QaError(f"JSON object required: {path}")
    return value


def scenario_identity(root: Path, scenario_path: Path | None = None) -> str:
    path = scenario_path if scenario_path is not None else root / SCENARIO
    scenario = read_json(path)
    if set(scenario) != {"failure", "happy", "red", "red_exempt", "schema", "task"}:
        raise QaError("task-4 scenario keys mismatch")
    if scenario["schema"] != 1 or scenario["task"] != 4 or scenario["red_exempt"] is not False:
        raise QaError("task-4 scenario identity mismatch")
    for bucket, commands in EXPECTED_COMMANDS.items():
        entries = scenario[bucket]
        if not isinstance(entries, list):
            raise QaError(f"task-4 {bucket} scenarios must be a list")
        actual: list[str] = []
        for entry in entries:
            if not isinstance(entry, dict) or not isinstance(entry.get("command"), str):
                raise QaError(f"task-4 {bucket} entry malformed")
            actual.append(entry["command"])
        if tuple(actual) != commands:
            raise QaError(f"task-4 {bucket} command drift")
    return sha256_path(path)


def fixture_records(root: Path) -> list[dict[str, JsonValue]]:
    return [
        {"path": relative, "sha256": sha256_path(root / relative)}
        for relative in sorted(FIXTURE_PATHS)
    ]


def fixture_digest(records: list[dict[str, JsonValue]]) -> str:
    payload = "".join(f"{record['path']}\0{record['sha256']}\n" for record in records)
    return hashlib.sha256(payload.encode()).hexdigest()


def fixture_patch(root: Path, staged: bool) -> str:
    arguments = ["diff", "--binary"]
    arguments.extend(["--cached"] if staged else [f"{BASELINE}..HEAD"])
    arguments.extend(["--", *FIXTURE_PATHS])
    return hashlib.sha256(git_bytes(root, *arguments)).hexdigest()


def fixture_tree(root: Path, *, flip_executable: str | None = None) -> str:
    descriptor, index_name = tempfile.mkstemp(prefix="mlc-task4-index-")
    os.close(descriptor)
    os.unlink(index_name)
    environment = {**os.environ, "GIT_INDEX_FILE": index_name}
    try:
        git(root, "read-tree", BASELINE, env=environment)
        git(root, "add", "--", *FIXTURE_PATHS, env=environment)
        for relative in FIXTURE_PATHS:
            entry = git(root, "ls-tree", "HEAD", "--", relative)
            mode = entry.split(maxsplit=1)[0] if entry else ""
            if relative == flip_executable:
                mode = "100644" if mode == "100755" else "100755" if mode == "100644" else mode
            if mode not in {"100644", "100755"}:
                raise QaError(f"unsupported receiving HEAD fixture mode: {relative}")
            git(
                root,
                "update-index",
                "--chmod=+x" if mode == "100755" else "--chmod=-x",
                relative,
                env=environment,
            )
        return git(root, "write-tree", env=environment)
    finally:
        Path(index_name).unlink(missing_ok=True)


def create_receipt(root: Path, evidence: Path, proof_exit: int) -> str:
    records = fixture_records(root)
    proof = evidence / "task-4-red-proof.log"
    document: dict[str, JsonValue] = {
        "authoritative": False,
        "baseline_sha": BASELINE,
        "command": RED_COMMAND,
        "current_head": git(root, "rev-parse", "HEAD"),
        "expected_exit": 86,
        "fixture_digest": fixture_digest(records),
        "fixture_files": records,
        "fixture_patch_sha256": fixture_patch(root, staged=True),
        "fixtures": "mlc_graph_core,mlc_zero_graph",
        "proof_exit": proof_exit,
        "proof_log_sha256": sha256_path(proof),
        "receipt_kind": "development_red",
        "scenario_path": SCENARIO,
        "scenario_sha256": scenario_identity(root),
        "schema": 2,
        "staged_write_tree": git(root, "write-tree"),
        "status": "evidence_bound_expected_failure",
        "task": 4,
    }
    receipt = evidence / "task-4-development-red.json"
    publish_json(receipt, document, force_weak=True)
    return sha256_path(receipt)


def verify_receipt(root: Path, evidence: Path, scenario_path: Path | None = None) -> str:
    receipt = evidence / "task-4-development-red.json"
    receipt_bytes = read_regular(receipt)
    document = json.loads(receipt_bytes)
    if not isinstance(document, dict):
        raise QaError(f"JSON object required: {receipt}")
    records = fixture_records(root)
    expected: dict[str, JsonValue] = {
        "authoritative": False,
        "baseline_sha": BASELINE,
        "command": RED_COMMAND,
        "current_head": BASELINE,
        "expected_exit": 86,
        "fixture_digest": fixture_digest(records),
        "fixture_files": records,
        "fixture_patch_sha256": fixture_patch(root, staged=False),
        "fixtures": "mlc_graph_core,mlc_zero_graph",
        "proof_exit": document.get("proof_exit"),
        "proof_log_sha256": sha256_path(evidence / "task-4-red-proof.log"),
        "receipt_kind": "development_red",
        "scenario_path": SCENARIO,
        "scenario_sha256": scenario_identity(root, scenario_path),
        "schema": 2,
        "staged_write_tree": fixture_tree(root),
        "status": "evidence_bound_expected_failure",
        "task": 4,
    }
    publication = document.pop("publication", None)
    if set(document) != set(expected):
        raise QaError("task-4 RED receipt schema mismatch")
    if not isinstance(expected["proof_exit"], int) or expected["proof_exit"] == 0:
        raise QaError("task-4 RED proof exit mismatch")
    for field, value in expected.items():
        if document.get(field) != value:
            raise QaError(f"task-4 RED receipt linkage mismatch: {field}")
    if publication != {
        "atomic_visibility": False,
        "directory_fsync": True,
        "file_fsync": True,
        "immutable": False,
        "mode": "exclusive_final_inode_weaker_exfat",
    }:
        raise QaError("task-4 RED publication mismatch")
    return hashlib.sha256(receipt_bytes).hexdigest()


def main() -> int:
    if len(sys.argv) not in {4, 5}:
        return 64
    action, root_text, evidence_text = sys.argv[1:4]
    root = Path(root_text).resolve(strict=True)
    evidence = Path(evidence_text).resolve(strict=True)
    try:
        match action:
            case "create":
                print(create_receipt(root, evidence, int(sys.argv[4])))
            case "verify":
                override = Path(sys.argv[4]) if len(sys.argv) == 5 else None
                print(verify_receipt(root, evidence, override))
            case unreachable:
                assert_never(unreachable)
    except (QaError, OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"task-4 evidence failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
