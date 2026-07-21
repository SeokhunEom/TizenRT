#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# ///
# ─── How to run ───
# uv run tools/test_mem_leak_checker_task6_validate.py

from __future__ import annotations

import copy
from dataclasses import replace
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mem_leak_checker_task6_manifest import (
    BASELINE_SHA,
    EVIDENCE_PATH,
    NORMALIZED_PLAN_SHA256,
    PLAN_PATH,
    SESSION_ID,
    SOURCE_PATHS,
    WORK_ID,
)
from mem_leak_checker_task6_output import DARWIN_LD_WARNING
from mem_leak_checker_task6_route_tests import test_codegraph, test_route_preflight
from mem_leak_checker_task6_security_tests import test_security_boundaries
from mem_leak_checker_task6_validate_test_support import (
    boulder_mutation_count,
    make_fixture,
    receipt_value,
    run_git,
)
from mem_leak_checker_task6_validate import (
    ContextSnapshot,
    ContractError,
    ExpectedContext,
    Observation,
    canonical_json,
    derive_context,
    parse_receipt_v2,
    validate_receipt_context,
)
from mem_leak_checker_task6_types import JsonValue


def reject_receipt(value: dict[str, JsonValue]) -> None:
    try:
        parse_receipt_v2(canonical_json(value))
    except ContractError:
        return
    raise AssertionError("receipt mutation accepted")


def reject_context(expected: ExpectedContext) -> None:
    try:
        derive_context(expected)
    except ContractError:
        return
    raise AssertionError("context mutation accepted")


def reject_receipt_context(
    value: dict[str, JsonValue],
    snapshot: ContextSnapshot,
    observed: tuple[tuple[str, Observation], ...] | None = None,
) -> None:
    try:
        document = parse_receipt_v2(canonical_json(value))
        validate_receipt_context(document, snapshot, observed)
    except ContractError:
        return
    raise AssertionError("receipt context mutation accepted")


def test_receipt_contract(root: Path, expected: ExpectedContext) -> int:
    snapshot = derive_context(expected)
    value, observed = receipt_value(snapshot)
    document = parse_receipt_v2(canonical_json(value))
    validate_receipt_context(document, snapshot, observed)
    count = 0
    for key in tuple(value):
        mutant = copy.deepcopy(value)
        del mutant[key]
        reject_receipt(mutant)
        count += 1
    reject_receipt({**value, "additional": True})
    count += 1
    type_mutations: dict[str, JsonValue] = {
        "schema_version": True, "task": False, "runtime_claim": 0,
        "receiving_sha": 1, "external_state_exclusions": {},
        "publication": [], "observations": [], "scenario_commands": [],
        "threat_model": [],
    }
    for key, replacement in type_mutations.items():
        mutant = copy.deepcopy(value)
        mutant[key] = replacement
        reject_receipt(mutant)
        count += 1
    for container in ("boulder", "scenario_commands", "observations", "publication", "threat_model"):
        nested = value[container]
        assert type(nested) is dict
        for key in tuple(nested):
            mutant = copy.deepcopy(value)
            del mutant[container][key]
            reject_receipt(mutant)
            count += 1
        mutant = copy.deepcopy(value)
        mutant[container]["additional"] = True
        reject_receipt(mutant)
        count += 1
    for key in tuple(value["boulder"]):
        mutant = copy.deepcopy(value)
        mutant["boulder"][key] = True
        reject_receipt(mutant)
        count += 1
    for key in tuple(value["publication"]):
        mutant = copy.deepcopy(value)
        mutant["publication"][key] = 1
        reject_receipt(mutant)
        count += 1
    for key in tuple(value["threat_model"]):
        mutant = copy.deepcopy(value)
        mutant["threat_model"][key] = "wrong"
        reject_receipt(mutant)
        count += 1
    for kind in ("red", "happy", "failure", "fatal"):
        mutant = copy.deepcopy(value)
        mutant["scenario_commands"][kind] = "wrong"
        reject_receipt_context(mutant, snapshot)
        count += 1
    for kind in ("happy", "failure", "fatal"):
        fields = value["observations"][kind]
        assert type(fields) is dict
        for field in tuple(fields):
            mutant = copy.deepcopy(value)
            del mutant["observations"][kind][field]
            reject_receipt(mutant)
            count += 1
        mutant = copy.deepcopy(value)
        mutant["observations"][kind]["additional"] = True
        reject_receipt(mutant)
        count += 1
    for key in (
        "receiving_sha", "receiving_tree", "branch", "root", "worktree",
        "baseline_sha", "normalized_plan_sha256", "scenario_sha256",
        "receiving_commit_source_sha256",
    ):
        mutant = copy.deepcopy(value)
        mutant[key] = "0" * (40 if key in {"receiving_sha", "receiving_tree"} else 64)
        if key in {"branch", "root", "worktree", "baseline_sha"}:
            mutant[key] = "wrong"
        reject_receipt_context(mutant, snapshot)
        count += 1
    for key in ("work_id", "plan", "session", "evidence_directory"):
        mutant = copy.deepcopy(value)
        mutant["boulder"][key] = "wrong"
        reject_receipt_context(mutant, snapshot)
        count += 1
    for kind in ("happy", "failure", "fatal"):
        for field, replacement in (
            ("command", "misleading success"), ("exit", 7),
            ("stdout_sha256", "0" * 64), ("stderr_sha256", "0" * 64),
            ("records_sha256", "0" * 64),
            ("stdout", "MLC_TASK6_MISLEADING status=PASS\n"),
            ("stderr", "misleading stderr\n"),
        ):
            mutant = copy.deepcopy(value)
            mutant["observations"][kind][field] = replacement
            reject_receipt_context(mutant, snapshot, observed)
            count += 1
    for kind in ("happy", "failure", "fatal"):
        mutant = copy.deepcopy(value)
        warning = DARWIN_LD_WARNING + b"\n"
        mutant["observations"][kind]["stderr"] = warning.decode()
        mutant["observations"][kind]["stderr_sha256"] = hashlib.sha256(warning).hexdigest()
        reject_receipt_context(mutant, snapshot)
        count += 1
    malformed = canonical_json(value)[:-1]
    try:
        parse_receipt_v2(malformed)
    except ContractError:
        count += 1
    else:
        raise AssertionError("noncanonical receipt accepted")
    duplicate = canonical_json(value).replace(b'{"baseline_sha"', b'{"task":6,"baseline_sha"', 1)
    try:
        parse_receipt_v2(duplicate)
    except ContractError:
        count += 1
    else:
        raise AssertionError("duplicate-key receipt accepted")
    old_document = parse_receipt_v2(canonical_json(value))
    (root / SOURCE_PATHS[0]).write_text("new receiving source\n")
    run_git(root, "add", SOURCE_PATHS[0])
    run_git(root, "commit", "-q", "-m", "receiving source drift")
    reject_receipt_context(value, derive_context(expected))
    assert old_document.snapshot.receiving_tree != derive_context(expected).receiving_tree
    count += 1
    return count


def test_context_mutations(parent: Path) -> int:
    count = 0
    mutations = (
        ("boulder-missing", lambda root, expected: (root / ".omo/boulder.json").write_text("{}")),
        ("plan-value", lambda root, expected: (root / expected.plan_path).write_text("changed\n")),
        ("session", lambda root, expected: (root / ".omo/start-work/ledger.jsonl").write_text('{}\n')),
        ("tracked-dirty", lambda root, expected: (root / SOURCE_PATHS[0]).write_text("dirty\n")),
        ("staged-dirty", lambda root, expected: ((root / SOURCE_PATHS[0]).write_text("dirty\n"), run_git(root, "add", SOURCE_PATHS[0]))),
        ("untracked", lambda root, expected: (root / "other.txt").write_text("dirty\n")),
        ("ignored", lambda root, expected: (root / "ignored.tmp").write_text("dirty\n")),
        ("arbitrary-omo-directory", lambda root, expected: (root / ".omo/arbitrary").mkdir()),
        ("plan-symlink", lambda root, expected: ((root / expected.plan_path).unlink(), (root / expected.plan_path).symlink_to(root / SOURCE_PATHS[0]))),
        ("boulder-symlink", lambda root, expected: ((root / ".omo/boulder.json").unlink(), (root / ".omo/boulder.json").symlink_to(root / SOURCE_PATHS[0]))),
        ("ledger-symlink", lambda root, expected: ((root / ".omo/start-work/ledger.jsonl").unlink(), (root / ".omo/start-work/ledger.jsonl").symlink_to(root / SOURCE_PATHS[0]))),
    )
    for name, mutate in mutations:
        case = parent / name
        case.mkdir()
        root, expected = make_fixture(case)
        mutate(root, expected)
        reject_context(expected)
        count += 1
    for field, value in (
        ("branch", "wrong"), ("baseline_sha", "0" * 40),
        ("work_id", "wrong"), ("plan_path", "wrong"),
        ("session_id", "wrong"), ("evidence_path", "/tmp/alternate"),
        ("normalized_plan_sha256", "0" * 64),
    ):
        case = parent / f"expected-{field}"
        case.mkdir()
        _, expected = make_fixture(case)
        values = {item: getattr(expected, item) for item in expected.__dataclass_fields__}
        values[field] = value
        reject_context(ExpectedContext(**values))
        count += 1
    case = parent / "boulder-fields"
    case.mkdir()
    root, expected = make_fixture(case)
    count += boulder_mutation_count(root, expected, reject_context)
    return count


def test_receiving_worktree_portability(parent: Path) -> None:
    source_parent = parent / "source"
    source_parent.mkdir()
    source, source_expected = make_fixture(source_parent)
    receiving = parent / "receiving"
    subprocess.run(
        ["/usr/bin/git", "clone", "-q", "--no-local", str(source), str(receiving)],
        check=True,
    )
    run_git(receiving, "checkout", "-q", "-b", "fixture/receiving")
    shutil.copytree(source / ".omo", receiving / ".omo")
    receiving_expected = replace(
        source_expected,
        root=receiving.resolve(),
        branch="fixture/receiving",
    )
    boulder_path = receiving / ".omo/boulder.json"
    boulder = json.loads(boulder_path.read_text())
    boulder["works"][receiving_expected.work_id]["worktree_path"] = str(
        receiving_expected.root
    )
    boulder_path.write_text(json.dumps(boulder))

    snapshot = derive_context(receiving_expected)
    assert snapshot.receiving_tree == run_git(source, "rev-parse", "HEAD^{tree}")
    reject_context(replace(receiving_expected, root=parent.resolve()))
    reject_context(replace(receiving_expected, branch="fixture/wrong"))
    boulder["works"][receiving_expected.work_id]["worktree_path"] = str(source.resolve())
    boulder_path.write_text(json.dumps(boulder))
    reject_context(receiving_expected)


def test_installed_production_context_is_portable(parent: Path) -> None:
    root, expected = make_fixture(parent)
    tools = root / "tools"
    source_tools = Path(__file__).resolve().parent
    for name in (
        "mem_leak_checker_task6_context.py",
        "mem_leak_checker_task6_files.py",
        "mem_leak_checker_task6_git_trust.py",
        "mem_leak_checker_task6_manifest.py",
        "mem_leak_checker_task6_schema.py",
        "mem_leak_checker_task6_types.py",
        "mem_leak_checker_task6_output.py",
    ):
        shutil.copy2(source_tools / name, tools / name)
    boulder = {
        "schema_version": 2,
        "active_work_id": WORK_ID,
        "works": {
            WORK_ID: {
                "work_id": WORK_ID,
                "active_plan": PLAN_PATH,
                "plan_name": "mem-leak-checker-hardening",
                "session_ids": [SESSION_ID],
                "status": "active",
                "worktree_path": str(root.resolve()),
            }
        },
    }
    (root / ".omo/boulder.json").write_text(json.dumps(boulder))
    code = (
        "from pathlib import Path; import sys; "
        f"sys.path.insert(0, {str(tools)!r}); "
        "from mem_leak_checker_task6_context import production_context; "
        f"value = production_context(Path({str(root.resolve())!r})); "
        "print(value.root); print(value.branch); print(value.baseline_sha); "
        "print(value.evidence_path); print(value.normalized_plan_sha256)"
    )
    environment = dict(os.environ)
    environment.pop("MLC_TASK6_CONTEXT_FIXTURE", None)
    result = subprocess.run(
        [sys.executable, "-I", "-B", "-c", code],
        cwd=root,
        env=environment,
        capture_output=True,
        text=True,
        check=True,
    )
    assert result.stdout.splitlines() == [
        str(root.resolve()), expected.branch, BASELINE_SHA, EVIDENCE_PATH,
        NORMALIZED_PLAN_SHA256,
    ]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="mlc-task6-validator.", dir="/tmp") as directory:
        parent = Path(directory)
        root, expected = make_fixture(parent)
        receipt_negatives = test_receipt_contract(root, expected)
        context_parent = parent / "contexts"
        context_parent.mkdir()
        context_negatives = test_context_mutations(context_parent)
        portability_parent = parent / "portability"
        portability_parent.mkdir()
        test_receiving_worktree_portability(portability_parent)
        installed_parent = parent / "installed"
        installed_parent.mkdir()
        test_installed_production_context_is_portable(installed_parent)
        codegraph_parent = parent / "codegraph"
        codegraph_parent.mkdir()
        codegraph_negatives = test_codegraph(codegraph_parent)
        route_parent = parent / "route"
        route_parent.mkdir()
        route_cases = test_route_preflight(route_parent)
        security_cases = test_security_boundaries(parent)
    print(
        "MLC_TASK6_VALIDATOR status=PASS schema=2 "
        f"receipt_negatives={receipt_negatives} context_negatives={context_negatives} "
        f"codegraph_cases={codegraph_negatives} route_cases={route_cases} "
        f"security_cases={security_cases}"
    )


if __name__ == "__main__":
    main()
