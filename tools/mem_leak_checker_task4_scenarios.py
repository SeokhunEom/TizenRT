# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# ─── How to run ───
# uv run tools/mem_leak_checker_task4_scenarios.py seal|negative <root> <evidence-dir>
from __future__ import annotations

import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import assert_never

from mem_leak_checker_qa_core import JsonValue, QaError, publish_json, sha256_path
from mem_leak_checker_task4_evidence import SCENARIO, fixture_tree, read_json, scenario_identity, verify_receipt


def write_exclusive(path: Path, payload: bytes) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
    descriptor = os.open(path.name, flags, 0o600, dir_fd=directory)
    try:
        offset = 0
        while offset < len(payload):
            written = os.write(descriptor, payload[offset:])
            if written <= 0:
                raise QaError("development artifact write made no progress")
            offset += written
        os.fsync(descriptor)
        os.fsync(directory)
    finally:
        os.close(descriptor)
        os.close(directory)


def require_entry(entry: JsonValue, bucket: str) -> tuple[str, int | None, tuple[str, ...]]:
    if not isinstance(entry, dict) or not isinstance(entry.get("command"), str):
        raise QaError(f"task-4 {bucket} entry malformed")
    expected_exit = entry.get("expected_exit")
    if expected_exit is not None and not isinstance(expected_exit, int):
        raise QaError(f"task-4 {bucket} expected exit malformed")
    records_value = entry.get("expected_records", [])
    if not isinstance(records_value, list) or not all(isinstance(value, str) for value in records_value):
        raise QaError(f"task-4 {bucket} records malformed")
    return entry["command"], expected_exit, tuple(records_value)


def execute_entry(root: Path, evidence: Path, entry: JsonValue, bucket: str) -> tuple[str, bytes]:
    command, expected_exit, expected_records = require_entry(entry, bucket)
    environment = {**os.environ, "MLC_TASK4_EVIDENCE_DIR": str(evidence)}
    completed = subprocess.run(
        shlex.split(command),
        cwd=root,
        env=environment,
        capture_output=True,
        check=False,
    )
    output = completed.stdout + completed.stderr
    required_exit = 0 if expected_exit is None else expected_exit
    if completed.returncode != required_exit:
        detail = output.decode(errors="replace").strip()
        raise QaError(f"task-4 scenario exit mismatch: {command}: {detail}")
    text = output.decode(errors="replace")
    for record in expected_records:
        if record not in text:
            raise QaError(f"task-4 scenario record missing: {record}")
    if isinstance(entry, dict):
        prefix = entry.get("expected_record_prefix")
        if isinstance(prefix, str) and not any(line.startswith(prefix) for line in text.splitlines()):
            raise QaError(f"task-4 scenario prefix missing: {prefix}")
        status = entry.get("expected_status")
        if isinstance(status, str) and f"status={status}" not in text:
            raise QaError(f"task-4 scenario status missing: {status}")
        fields = entry.get("required_fields", [])
        if not isinstance(fields, list) or not all(isinstance(field, str) for field in fields):
            raise QaError("task-4 required fields malformed")
        for field in fields:
            token = field if "=" in field else f"{field}="
            if token not in text:
                raise QaError(f"task-4 required field missing: {field}")
    return command, output


def fixture_negative(root: Path, selection: str) -> None:
    completed = subprocess.run(
        [str(root / "tools/test_mem_leak_checker_graph.sh"), "--fixtures", selection],
        cwd=root,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 64 or completed.stdout or completed.stderr:
        raise QaError(f"fixture validation emitted output: {selection}")


def fixture_mode_negatives(root: Path, evidence: Path) -> None:
    paths = (
        "tools/mem_leak_checker_task4_qa.sh",
        "tools/test_mem_leak_checker_graph.sh",
        "tools/mem_leak_checker_task4_evidence.py",
    )
    canonical_tree = fixture_tree(root)
    for relative in paths:
        mutated_tree = fixture_tree(root, flip_executable=relative)
        if mutated_tree == canonical_tree:
            raise QaError(f"task-4 fixture mode mutation was ineffective: {relative}")
        with tempfile.TemporaryDirectory(prefix="mlc-task4-mode-") as directory:
            mutated = Path(directory).resolve() / "evidence"
            shutil.copytree(evidence, mutated)
            receipt = mutated / "task-4-development-red.json"
            document = read_json(receipt)
            document["staged_write_tree"] = mutated_tree
            receipt.write_text(json.dumps(document, sort_keys=True), encoding="utf-8")
            try:
                verify_receipt(root, mutated)
            except QaError:
                mode_rejected = True
            else:
                mode_rejected = False
            if not mode_rejected:
                raise QaError(f"task-4 fixture mode mutation accepted: {relative}")


def mutation_negatives(root: Path, evidence: Path) -> None:
    for selection in (
        "mlc_graph_core,unknown",
        "mlc_graph_core,mlc_graph_core",
        "",
        "mlc_graph_core,",
        "mlc_graph_core,,mlc_zero_graph",
    ):
        fixture_negative(root, selection)
    fixture_mode_negatives(root, evidence)
    mutations: tuple[tuple[str, JsonValue], ...] = (
        ("fixture_digest", "0" * 64),
        ("fixture_patch_sha256", "0" * 64),
        ("staged_write_tree", "0" * 40),
        ("scenario_sha256", "0" * 64),
        ("current_head", "0" * 40),
        ("command", "false"),
        ("expected_exit", 0),
        ("publication", {"mode": "strong_claim"}),
    )
    for field, value in mutations:
        with tempfile.TemporaryDirectory(prefix="mlc-task4-receipt-") as directory:
            mutated = Path(directory).resolve() / "evidence"
            shutil.copytree(evidence, mutated)
            receipt = mutated / "task-4-development-red.json"
            document = read_json(receipt)
            document[field] = value
            receipt.write_text(json.dumps(document, sort_keys=True), encoding="utf-8")
            try:
                verify_receipt(root, mutated)
            except QaError:
                receipt_rejected = True
            else:
                receipt_rejected = False
            if not receipt_rejected:
                raise QaError(f"mutated task-4 receipt accepted: {field}")
    with tempfile.TemporaryDirectory(prefix="mlc-task4-scenario-") as directory:
        scenario = Path(directory).resolve() / "task-4.json"
        document = read_json(root / SCENARIO)
        happy = document.get("happy")
        if not isinstance(happy, list) or not happy or not isinstance(happy[0], dict):
            raise QaError("canonical task-4 happy scenario malformed")
        happy[0]["command"] = "false"
        scenario.write_text(json.dumps(document, sort_keys=True), encoding="utf-8")
        try:
            verify_receipt(root, evidence, scenario)
        except QaError:
            scenario_rejected = True
        else:
            scenario_rejected = False
        if not scenario_rejected:
            raise QaError("mutated task-4 scenario accepted")


def seal_development(root: Path, evidence: Path) -> str:
    receipt_hash = verify_receipt(root, evidence)
    scenario_hash = scenario_identity(root)
    scenario = read_json(root / SCENARIO)
    logs = evidence / "task-4-development-logs"
    logs.mkdir(mode=0o700)
    artifacts: dict[str, JsonValue] = {}
    command_count = 0
    for bucket in ("happy", "failure"):
        entries = scenario[bucket]
        if not isinstance(entries, list):
            raise QaError(f"task-4 {bucket} scenarios malformed")
        for entry in entries:
            command, output = execute_entry(root, evidence, entry, bucket)
            name = f"{bucket}-{command_count}.log"
            write_exclusive(logs / name, output)
            artifacts[name] = {
                "command": command,
                "sha256": hashlib.sha256(output).hexdigest(),
            }
            command_count += 1
    expected_count = sum(len(scenario[bucket]) for bucket in ("happy", "failure"))
    if command_count != expected_count:
        raise QaError("task-4 scenario omission")
    document: dict[str, JsonValue] = {
        "artifacts": artifacts,
        "authoritative": False,
        "command_count": command_count,
        "development_only": True,
        "qemu_status": "deferred_unexecuted_baseline_link_failure",
        "red_receipt_sha256": receipt_hash,
        "scenario_sha256": scenario_hash,
        "schema": 1,
        "source_head": subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip(),
        "status": "PASS",
        "task": 4,
    }
    seal = evidence / "task-4-development-seal.json"
    publish_json(seal, document, force_weak=True)
    return sha256_path(seal)


def main() -> int:
    if len(sys.argv) != 4:
        return 64
    action, root_text, evidence_text = sys.argv[1:]
    root = Path(root_text).resolve(strict=True)
    evidence = Path(evidence_text).resolve(strict=True)
    try:
        match action:
            case "negative":
                mutation_negatives(root, evidence)
                print("MLC_QA_TASK4_NEGATIVE cases=invalid-trailing,duplicate,empty,surplus,fixture-mode-mutation,receipt-mutation,scenario-mutation status=PASS")
            case "seal":
                print(seal_development(root, evidence))
            case unreachable:
                assert_never(unreachable)
    except (QaError, OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"task-4 scenarios failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
