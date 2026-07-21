from __future__ import annotations

import hashlib
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mem_leak_checker_task6_context import (
    derive_context,
    production_context,
    validate_receipt_context,
)
from mem_leak_checker_task6_schema import (
    QEMU_STATUS,
    VERDICT,
    canonical_json,
    command_argv,
    fail,
    observation,
    parse_receipt_v2,
    parse_scenario_v1,
)
from mem_leak_checker_task6_git_trust import (
    capture as capture_git_admin,
    run as git_run,
    sanitized_process_environment,
    validate as validate_git_trust,
)
from mem_leak_checker_task6_publish import publish
from mem_leak_checker_task6_files import read_path
from mem_leak_checker_task6_types import (
    ContextSnapshot, ContractError, JsonValue, Observation,
)
from mem_leak_checker_task6_manifest import THREAT_MODEL


def receipt_value(
    snapshot: ContextSnapshot,
    observed: tuple[tuple[str, Observation], ...],
) -> dict[str, JsonValue]:
    values: dict[str, JsonValue] = {}
    for kind, item in observed:
        values[kind] = {
            "command": item.command, "exit": item.exit_code,
            "stdout": item.stdout, "stderr": item.stderr,
            "stdout_sha256": item.stdout_sha256,
            "stderr_sha256": item.stderr_sha256,
            "records_sha256": item.records_sha256,
        }
    return {
        "schema_version": 2, "task": 6, "status": VERDICT,
        "qemu": QEMU_STATUS, "runtime_claim": False,
        "hardware_validation": "skipped_by_user",
        "receiving_sha": snapshot.receiving_sha,
        "receiving_tree": snapshot.receiving_tree,
        "branch": snapshot.branch, "root": snapshot.root,
        "worktree": snapshot.root, "baseline_sha": snapshot.baseline_sha,
        "boulder": {
            "schema_version": 2, "work_id": snapshot.work_id,
            "plan": snapshot.plan_path, "session": snapshot.session_id,
            "evidence_directory": snapshot.evidence_path,
        },
        "normalized_plan_sha256": snapshot.normalized_plan_sha256,
        "scenario_sha256": snapshot.scenario_sha256,
        "receiving_commit_source_sha256": snapshot.source_sha256,
        "scenario_commands": dict(snapshot.commands),
        "observations": values,
        "external_state_exclusions": list(snapshot.external_state_exclusions),
        "threat_model": THREAT_MODEL,
        "publication": {
            "mode": "exclusive_final_inode_weaker_exfat",
            "file_fsync": True, "directory_fsync": True,
            "immutable": False, "atomic_visibility": False,
        },
    }


def seal(root: Path, source: str) -> str:
    expected = production_context(root)
    validate_git_trust(root)
    admin_before = capture_git_admin(root)
    receiving_sha = git_run(root, "rev-parse", source).stdout.decode().strip()
    receipt = root / expected.evidence_path / f"task-6-post-integration-{receiving_sha}.json"
    configured = os.environ.get("MLC_TASK6_EVIDENCE_DIR")
    if configured is not None and Path(configured) != receipt.parent:
        raise fail("MLC_TASK6_EVIDENCE_DIR", "alternate evidence directory forbidden")
    relative_receipt = receipt.relative_to(root).as_posix()
    before = derive_context(expected, relative_receipt if receipt.exists() else None)
    if receiving_sha != before.receiving_sha:
        raise fail("source", "current HEAD required")
    if receipt.exists():
        validate_receipt_context(parse_receipt_v2(read_path(receipt)), before)
    scenario_blob = git_run(root, "show", f"{receiving_sha}:tools/mem_leak_checker_scenarios/task-6.json").stdout
    scenario = parse_scenario_v1(scenario_blob)
    observed: list[tuple[str, Observation]] = []
    for case in scenario.scenarios:
        result = subprocess.run(
            command_argv(case.command), cwd=root, shell=False, capture_output=True,
            env=sanitized_process_environment(),
        )
        observed.append((case.kind, observation(case.command, result.returncode, result.stdout, result.stderr)))
    after = derive_context(expected, relative_receipt if receipt.exists() else None)
    if after != before or capture_git_admin(root) != admin_before:
        raise fail("context", "receiving context changed during QA")
    value = receipt_value(after, tuple(observed))
    encoded = canonical_json(value)
    document = parse_receipt_v2(encoded)
    validate_receipt_context(document, after, tuple(observed))
    publish(receipt, encoded)
    final = derive_context(expected, relative_receipt)
    if final != before or capture_git_admin(root) != admin_before:
        raise fail("context", "receiving context changed during publication")
    validate_receipt_context(parse_receipt_v2(read_path(receipt)), final, tuple(observed))
    return hashlib.sha256(encoded).hexdigest()


def main() -> int:
    if len(sys.argv) != 6 or sys.argv[1] != "seal" or sys.argv[2] != "--root" or sys.argv[4] != "--source":
        print(f"usage: {sys.argv[0]} seal --root ROOT --source REV", file=sys.stderr)
        return 64
    print(seal(Path(sys.argv[3]), sys.argv[5]))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ContractError, OSError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
