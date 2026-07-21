from __future__ import annotations

import copy
from collections.abc import Callable
import hashlib
import json
from pathlib import Path
import subprocess

from mem_leak_checker_task6_manifest import SOURCE_PATHS, THREAT_MODEL
from mem_leak_checker_task6_validate import (
    ContextSnapshot, ExpectedContext, Observation, canonical_json,
    observation, parse_scenario_v1,
)
from mem_leak_checker_task6_types import JsonValue


def scenario_value() -> dict[str, JsonValue]:
    cases: list[JsonValue] = []
    for kind, fixtures, suffix, record in (
        ("happy", ["mlc_domain_pin_production_path", "mlc_try_heap_fresh_accounting", "mlc_heap_release_nested_critical"], " --post-commit", "MLC_TASK6_HAPPY status=PASS"),
        ("failure", ["mlc_domain_unload_churn", "mlc_remote_critical_then_heap", "mlc_bounded_acquire_busy", "mlc_heap_preowned", "mlc_heap_accounting_fault", "mlc_heap_release_irqwaitlock_forbidden"], " --repeat 500", "MLC_TASK6_FAILURE status=PASS repeat=500"),
        ("fatal", ["mlc_heap_release_ownership_fatal", "mlc_domain_unpin_fatal"], None, "MLC_TASK6_FATAL status=PASS nonreturn=true"),
    ):
        joined = ",".join(fixtures)
        command = f"tools/mem_leak_checker_task6_qa.sh fatal --fixtures {joined}" if suffix is None else f"tools/mem_leak_checker_qa.sh qemu --task 6 --fixtures {joined}{suffix}"
        cases.append({"kind": kind, "fixtures": fixtures, "command": command, "expected_exit": 0, "expected_records": [record]})
    return {
        "schema": 1, "task": 6, "qemu": "deferred_unexecuted_baseline_link_failure",
        "red": {"fixture": "mlc_domain_pin_production_path", "command": "tools/mem_leak_checker_qa.sh red --task 6 --config qemu/tc_1m --fixture mlc_domain_pin_production_path", "expected_exit": 86, "expected_records": ["MLC_TASK6_RED expected_exit=86"]},
        "scenarios": cases,
    }


def run_git(root: Path, *args: str) -> str:
    return subprocess.run(["/usr/bin/git", "-C", str(root), *args], check=True, capture_output=True, text=True).stdout.strip()


def make_fixture(parent: Path) -> tuple[Path, ExpectedContext]:
    root = parent / "repository"
    root.mkdir()
    run_git(root, "init", "-q", "-b", "fixture/task6")
    run_git(root, "config", "user.email", "task6@example.invalid")
    run_git(root, "config", "user.name", "Task6 Fixture")
    for relative in SOURCE_PATHS:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"fixture {relative}\n")
    scenario_path = root / "tools/mem_leak_checker_scenarios/task-6.json"
    scenario_path.write_bytes(canonical_json(scenario_value()))
    (root / ".gitignore").write_text(".omo/\nignored.tmp\n.codegraph/**\n")
    run_git(root, "add", ".")
    run_git(root, "commit", "-q", "-m", "fixture")
    baseline = run_git(root, "rev-parse", "HEAD")
    plan_relative = ".omo/plans/mem-leak-checker-hardening.md"
    plan = root / plan_relative
    plan.parent.mkdir(parents=True)
    plan.write_text("# fixture\n\n- [x] 6. task\n")
    normalized = plan.read_bytes().replace(b"- [x] 6.", b"- [ ] 6.")
    expected = ExpectedContext(root.resolve(), "fixture/task6", baseline, "fixture-work", plan_relative, "codex:fixture", ".omo/start-work/artifacts/task-6-executor", hashlib.sha256(normalized).hexdigest())
    boulder = {"schema_version": 2, "active_work_id": expected.work_id, "works": {expected.work_id: {"work_id": expected.work_id, "active_plan": expected.plan_path, "plan_name": "mem-leak-checker-hardening", "session_ids": [expected.session_id], "status": "active", "worktree_path": str(expected.root)}}}
    (root / ".omo/boulder.json").write_text(json.dumps(boulder))
    ledger = root / ".omo/start-work/ledger.jsonl"
    ledger.parent.mkdir(parents=True, exist_ok=True)
    ledger.write_text(json.dumps({"event": "work-started", "task": "orchestration", "session_id": expected.session_id, "plan": expected.plan_path, "artifact": ".omo/boulder.json", "baseline_sha": baseline}) + "\n")
    return root, expected


def receipt_value(snapshot: ContextSnapshot) -> tuple[dict[str, JsonValue], tuple[tuple[str, Observation], ...]]:
    scenario = parse_scenario_v1((Path(snapshot.root) / "tools/mem_leak_checker_scenarios/task-6.json").read_bytes())
    observed: list[tuple[str, Observation]] = []
    values: dict[str, JsonValue] = {}
    for case in scenario.scenarios:
        item = observation(case.command, case.expected_exit, ("\n".join(case.expected_records) + "\n").encode(), b"")
        observed.append((case.kind, item))
        values[case.kind] = {"command": item.command, "exit": item.exit_code, "stdout": item.stdout, "stderr": item.stderr, "stdout_sha256": item.stdout_sha256, "stderr_sha256": item.stderr_sha256, "records_sha256": item.records_sha256}
    value: dict[str, JsonValue] = {
        "schema_version": 2, "task": 6, "status": "host_scenarios_sealed_qemu_explicitly_deferred", "qemu": "deferred_unexecuted_baseline_link_failure", "runtime_claim": False, "hardware_validation": "skipped_by_user", "receiving_sha": snapshot.receiving_sha, "receiving_tree": snapshot.receiving_tree, "branch": snapshot.branch, "root": snapshot.root, "worktree": snapshot.root, "baseline_sha": snapshot.baseline_sha,
        "boulder": {"schema_version": 2, "work_id": snapshot.work_id, "plan": snapshot.plan_path, "session": snapshot.session_id, "evidence_directory": snapshot.evidence_path},
        "normalized_plan_sha256": snapshot.normalized_plan_sha256, "scenario_sha256": snapshot.scenario_sha256, "receiving_commit_source_sha256": snapshot.source_sha256, "scenario_commands": dict(snapshot.commands), "observations": values, "external_state_exclusions": list(snapshot.external_state_exclusions),
        "publication": {"mode": "exclusive_final_inode_weaker_exfat", "file_fsync": True, "directory_fsync": True, "immutable": False, "atomic_visibility": False},
        "threat_model": THREAT_MODEL,
    }
    return value, tuple(observed)


def boulder_mutation_count(
    root: Path,
    expected: ExpectedContext,
    reject: Callable[[ExpectedContext], None],
) -> int:
    boulder_path = root / ".omo/boulder.json"
    canonical = json.loads(boulder_path.read_text())
    work = canonical["works"][expected.work_id]
    count = 0
    for container_name, keys in (
        ("top", tuple(canonical)),
        ("work", tuple(work)),
    ):
        for key in keys:
            for mutation in ("missing", "value", "type"):
                boulder_path.write_text(json.dumps(canonical))
                mutant = copy.deepcopy(json.loads(boulder_path.read_text()))
                container = mutant if container_name == "top" else mutant["works"][expected.work_id]
                if mutation == "missing":
                    del container[key]
                elif mutation == "type":
                    container[key] = True
                elif key == "schema_version":
                    container[key] = 3
                elif key == "works":
                    container[key] = {}
                elif key == "session_ids":
                    container[key] = ["wrong"]
                else:
                    container[key] = "wrong"
                boulder_path.write_text(json.dumps(mutant))
                reject(expected)
                count += 1
    for container_name in ("top", "work"):
        boulder_path.write_text(json.dumps(canonical))
        mutant = copy.deepcopy(json.loads(boulder_path.read_text()))
        container = mutant if container_name == "top" else mutant["works"][expected.work_id]
        container["additional"] = True
        boulder_path.write_text(json.dumps(mutant))
        reject(expected)
        count += 1
    boulder_path.write_text(json.dumps(canonical))
    return count
