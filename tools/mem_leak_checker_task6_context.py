from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import stat
from typing import Final

from mem_leak_checker_task6_manifest import (
    BASELINE_SHA, EVIDENCE_PATH, NORMALIZED_PLAN_SHA256, PLAN_PATH,
    SESSION_ID, SOURCE_PATHS, WORK_ID,
)
from mem_leak_checker_task6_git_trust import run as git_run, validate as validate_git_trust
from mem_leak_checker_task6_files import read_path, read_regular
from mem_leak_checker_task6_schema import canonical_json, fail, parse_scenario_v1
from mem_leak_checker_task6_types import (
    ContextSnapshot, ExpectedContext, Observation, ReceiptDocument,
)

PLAN_CHECKBOX_PATTERN: Final = re.compile(rb"(?m)^- \[(?: |x)\] (?=(?:[0-9]+|F[1-4])\.)")


def production_context(root: Path) -> ExpectedContext:
    module_root = Path(__file__).resolve().parent.parent
    canonical_root = root.resolve()
    if not root.is_absolute() or root != canonical_root or module_root != canonical_root:
        raise fail("context.root", "validator root must be its canonical worktree root")
    fixture_value = os.environ.get("MLC_TASK6_CONTEXT_FIXTURE")
    if fixture_value is not None:
        allowed_prefixes = ("/tmp/mlc-task6-", "/private/tmp/mlc-task6-")
        if not str(module_root).startswith(allowed_prefixes):
            raise fail("context.fixture", "fixture override forbidden outside private test root")
        try:
            fixture = json.loads(fixture_value)
        except json.JSONDecodeError as error:
            raise fail("context.fixture", "valid fixture JSON required") from error
        keys = {"root", "branch", "baseline_sha", "work_id", "plan_path", "session_id", "evidence_path", "normalized_plan_sha256"}
        if type(fixture) is not dict or set(fixture) != keys or any(type(value) is not str for value in fixture.values()):
            raise fail("context.fixture", "exact string fixture context required")
        if Path(fixture["root"]) != canonical_root:
            raise fail("context.fixture", "fixture root must contain validator")
        return ExpectedContext(Path(fixture["root"]), fixture["branch"], fixture["baseline_sha"], fixture["work_id"], fixture["plan_path"], fixture["session_id"], fixture["evidence_path"], fixture["normalized_plan_sha256"])

    try:
        boulder = json.loads(read_regular(canonical_root, ".omo/boulder.json"))
    except json.JSONDecodeError as error:
        raise fail(".omo/boulder.json", "valid JSON required") from error
    if (
        type(boulder) is not dict
        or set(boulder) != {"schema_version", "active_work_id", "works"}
        or boulder.get("schema_version") != 2
        or boulder.get("active_work_id") != WORK_ID
    ):
        raise fail(".omo/boulder.json", "exact active Task6 Boulder work required")
    works = boulder.get("works")
    if type(works) is not dict or set(works) != {WORK_ID}:
        raise fail(".omo/boulder.json", "exact active Task6 Boulder work required")
    work = works[WORK_ID]
    if type(work) is not dict or type(work.get("worktree_path")) is not str:
        raise fail(".omo/boulder.json", "Task6 worktree path required")
    boulder_root = Path(work["worktree_path"])
    if (
        not boulder_root.is_absolute()
        or boulder_root != boulder_root.resolve()
        or boulder_root != canonical_root
    ):
        raise fail("context.root", "active Boulder worktree must equal validator root")
    expected_work = {
        "work_id": WORK_ID,
        "active_plan": PLAN_PATH,
        "plan_name": "mem-leak-checker-hardening",
        "session_ids": [SESSION_ID],
        "status": "active",
        "worktree_path": str(boulder_root),
    }
    expected_boulder = {
        "schema_version": 2,
        "active_work_id": WORK_ID,
        "works": {WORK_ID: expected_work},
    }
    if boulder != expected_boulder:
        raise fail(".omo/boulder.json", "exact active Task6 Boulder work required")
    branch_result = git_run(
        canonical_root, "symbolic-ref", "--quiet", "--short", "HEAD", check=False,
    )
    branch = branch_result.stdout.decode().strip()
    if branch_result.returncode != 0 or not branch:
        raise fail("context.branch", "attached current branch required")
    return ExpectedContext(
        root=canonical_root, branch=branch, baseline_sha=BASELINE_SHA,
        work_id=WORK_ID, plan_path=PLAN_PATH, session_id=SESSION_ID,
        evidence_path=EVIDENCE_PATH, normalized_plan_sha256=NORMALIZED_PLAN_SHA256,
    )


def derive_current_context(expected: ExpectedContext) -> ContextSnapshot:
    receiving_sha = git(expected.root, "rev-parse", "HEAD").decode().strip()
    relative = f"{expected.evidence_path}/task-6-post-integration-{receiving_sha}.json"
    receipt = expected.root / relative
    snapshot = derive_context(expected, relative if receipt.exists() else None)
    if receipt.exists():
        from mem_leak_checker_task6_schema import parse_receipt_v2

        validate_receipt_context(parse_receipt_v2(read_path(receipt)), snapshot)
    return snapshot


def git(root: Path, *args: str) -> bytes:
    return git_run(root, *args).stdout


def safe_regular(root: Path, relative: str) -> bytes:
    return read_regular(root, relative)


def validate_cleanliness(root: Path, allowed_receipt: str | None) -> tuple[str, ...]:
    unstaged = git_run(root, "diff", "--quiet", check=False)
    staged = git_run(root, "diff", "--cached", "--quiet", check=False)
    if unstaged.returncode != 0 or staged.returncode != 0:
        raise fail("git", "tracked or staged changes forbidden")
    allowed = {".omo/boulder.json", PLAN_PATH, ".omo/start-work/ledger.jsonl"}
    if allowed_receipt is not None:
        allowed.add(allowed_receipt)
    allowed_directories = {
        ".omo", ".omo/plans", ".omo/start-work", ".omo/start-work/artifacts",
        EVIDENCE_PATH,
    }
    omo_root = root / ".omo"
    for current, directories, files in os.walk(omo_root, followlinks=False):
        for name in directories:
            path = Path(current) / name
            relative = path.relative_to(root).as_posix()
            if stat.S_ISLNK(path.lstat().st_mode) or relative not in allowed_directories:
                raise fail(relative, "unsanctioned orchestration directory")
        for name in files:
            path = Path(current) / name
            if not stat.S_ISREG(path.lstat().st_mode):
                raise fail(path.relative_to(root).as_posix(), "orchestration state must be regular")
    others = {os.fsdecode(item) for item in git(root, "ls-files", "--others", "-z").split(b"\0") if item}
    external: list[str] = []
    codegraph = root / ".codegraph"
    if codegraph.exists() or codegraph.is_symlink():
        mode = codegraph.lstat().st_mode
        if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode) or codegraph.resolve() != root / ".codegraph":
            raise fail(".codegraph", "real in-root directory required")
        if git(root, "ls-files", "-z", "--", ".codegraph"):
            raise fail(".codegraph", "tracked entries forbidden")
        for current, directories, files in os.walk(codegraph, followlinks=False):
            for name in (*directories, *files):
                path = Path(current) / name
                entry_mode = path.lstat().st_mode
                if stat.S_ISLNK(entry_mode) or not (stat.S_ISDIR(entry_mode) or stat.S_ISREG(entry_mode)):
                    raise fail(".codegraph", "symlinks and special files forbidden")
                relative = path.relative_to(root).as_posix()
                ignored = git_run(root, "check-ignore", "--quiet", "--", relative, check=False)
                if ignored.returncode != 0:
                    raise fail(relative, "Codegraph content must be ignored")
        external.append(".codegraph:unauthenticated_external_state")
    for relative in others:
        if relative.startswith(".codegraph/") and external:
            continue
        if relative not in allowed:
            raise fail(relative, "unsanctioned untracked or ignored path")
        safe_regular(root, relative)
    return tuple(external)


def derive_context(expected: ExpectedContext, allowed_receipt: str | None = None) -> ContextSnapshot:
    root = expected.root.resolve()
    if expected.evidence_path != EVIDENCE_PATH:
        raise fail("context.evidence", "exact Task6 evidence directory required")
    configured_evidence = os.environ.get("MLC_TASK6_EVIDENCE_DIR")
    if configured_evidence is not None and Path(configured_evidence) != root / expected.evidence_path:
        raise fail("MLC_TASK6_EVIDENCE_DIR", "alternate evidence directory forbidden")
    if str(root) != str(expected.root) or git(root, "rev-parse", "--show-toplevel").decode().strip() != str(root):
        raise fail("context.root", "exact worktree root required")
    validate_git_trust(root)
    branch = git(root, "branch", "--show-current").decode().strip()
    if branch != expected.branch:
        raise fail("context.branch", "exact branch required")
    external = validate_cleanliness(root, allowed_receipt)
    boulder_bytes = safe_regular(root, ".omo/boulder.json")
    plan_bytes = safe_regular(root, expected.plan_path)
    ledger_bytes = safe_regular(root, ".omo/start-work/ledger.jsonl")
    try:
        boulder = json.loads(boulder_bytes)
    except json.JSONDecodeError as error:
        raise fail(".omo/boulder.json", "valid JSON required") from error
    expected_work = {"work_id": expected.work_id, "active_plan": expected.plan_path, "plan_name": "mem-leak-checker-hardening", "session_ids": [expected.session_id], "status": "active", "worktree_path": str(root)}
    exact_boulder = {"schema_version": 2, "active_work_id": expected.work_id, "works": {expected.work_id: expected_work}}
    if type(boulder) is not dict or type(boulder.get("schema_version")) is not int or boulder != exact_boulder:
        raise fail(".omo/boulder.json", "exact Boulder schema 2 context required")
    plan_sha = hashlib.sha256(PLAN_CHECKBOX_PATTERN.sub(b"- [ ] ", plan_bytes)).hexdigest()
    if plan_sha != expected.normalized_plan_sha256:
        raise fail(expected.plan_path, "normalized plan identity mismatch")
    try:
        ledger = [json.loads(line) for line in ledger_bytes.decode().splitlines()]
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise fail(".omo/start-work/ledger.jsonl", "valid JSONL required") from error
    if not ledger or type(ledger[0]) is not dict:
        raise fail(".omo/start-work/ledger.jsonl", "work-started record required")
    first = ledger[0]
    if any(type(entry) is not dict or entry.get("session_id") != expected.session_id or entry.get("plan") != expected.plan_path for entry in ledger):
        raise fail(".omo/start-work/ledger.jsonl", "exact session and plan required")
    if first.get("event") != "work-started" or first.get("baseline_sha") != expected.baseline_sha or first.get("artifact") != ".omo/boulder.json":
        raise fail(".omo/start-work/ledger.jsonl", "exact baseline and Boulder identity required")
    ancestor = git_run(root, "merge-base", "--is-ancestor", expected.baseline_sha, "HEAD", check=False)
    if ancestor.returncode != 0:
        raise fail("context.baseline", "baseline must be an ancestor")
    receiving_sha = git(root, "rev-parse", "HEAD").decode().strip()
    receiving_tree = git(root, "rev-parse", "HEAD^{tree}").decode().strip()
    scenario_relative = "tools/mem_leak_checker_scenarios/task-6.json"
    scenario_blob = git(root, "show", f"{receiving_sha}:{scenario_relative}")
    scenario = parse_scenario_v1(scenario_blob)
    source = hashlib.sha256()
    for relative in SOURCE_PATHS:
        source.update(relative.encode() + b"\0" + git(root, "show", f"{receiving_sha}:{relative}"))
    commands = (("red", scenario.red.command), *((item.kind, item.command) for item in scenario.scenarios))
    return ContextSnapshot(str(root), branch, expected.baseline_sha, receiving_sha, receiving_tree, expected.work_id, expected.plan_path, expected.session_id, expected.evidence_path, plan_sha, hashlib.sha256(scenario_blob).hexdigest(), source.hexdigest(), commands, external)


def validate_receipt_context(document: ReceiptDocument, snapshot: ContextSnapshot, observed: tuple[tuple[str, Observation], ...] | None = None) -> None:
    if document.snapshot != snapshot:
        raise fail("receipt.context", "current receiving context drift")
    scenario_blob = git(Path(snapshot.root), "show", f"{snapshot.receiving_sha}:tools/mem_leak_checker_scenarios/task-6.json")
    cases = {item.kind: item for item in parse_scenario_v1(scenario_blob).scenarios}
    for kind, item in document.observations:
        case = cases[kind]
        records_sha = hashlib.sha256(canonical_json(list(case.expected_records))).hexdigest()
        empty_stderr_sha = hashlib.sha256(b"").hexdigest()
        if (
            item.command != case.command or item.exit_code != case.expected_exit
            or item.records_sha256 != records_sha or item.stderr
            or item.stderr_sha256 != empty_stderr_sha
        ):
            raise fail(f"$.observations.{kind}", "command, exit, transcript, or Task6 records mismatch")
    if observed is not None and document.observations != observed:
        raise fail("$.observations", "observed exit or output drift")
