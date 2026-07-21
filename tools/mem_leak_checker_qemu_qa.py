#!/usr/bin/env python3
# noqa: SIZE_OK — one CLI boundary owns context, scenario, self-test, and receipt parsing.
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Final, TypeVar, assert_never

from mem_leak_checker_preflight import run_preflight_case
from mem_leak_checker_qa_core import (
    JsonValue,
    PROMPT,
    QaError,
    canonical_bytes,
    parse_completed_transcript,
    publish_json,
    publication_record,
    run_checked,
    run_qemu,
    sha256_path,
    terminate_process_group,
    validate_root,
)

BASELINE: Final = "c93078ab05bb6463467669fb6ee19bb75ee7eaba"
WORK_ID: Final = "mem-leak-checker-hardening-257754dc"
PLAN: Final = ".omo/plans/mem-leak-checker-hardening.md"
SESSION: Final = "codex:257754dc-1fe6-461f-86d4-57a7d2b9fb8d"
BRANCH: Final = "codex/mem-leak-checker-hardening"
TASK3_BRANCH: Final = "codex/mlc-todo3"
TASK3_BASE: Final = "fe40a82f5dfe989c28e6da7ee64eb919e00149d9"
TASK3_EVIDENCE_RELATIVE: Final = ".omo/start-work/artifacts/task-3-executor"
TASK3_FIXTURE_PATHS: Final = tuple(
    sorted(
        {
            "os/kernel/debug/mem_leak_checker_core.c",
            "os/kernel/debug/mem_leak_checker_core.h",
            "os/kernel/debug/mem_leak_checker_core_internal.h",
            "os/kernel/debug/mem_leak_checker_index.c",
            "os/kernel/debug/tests/test_mem_leak_checker_core.c",
            "tools/mem_leak_checker_scenarios/task-3.json",
            "tools/test_mem_leak_checker_core.sh",
        }
    )
)
EVIDENCE_RELATIVE: Final = ".omo/start-work/artifacts/task-1-executor"
KNOWN_TODO_PATHS: Final = {
    "apps/examples/testcase/le_tc/kernel/Kconfig",
    "apps/examples/testcase/le_tc/kernel/Make.defs",
    "apps/examples/testcase/le_tc/kernel/kernel_tc_main.c",
    "apps/examples/testcase/le_tc/kernel/tc_internal.h",
    "apps/examples/testcase/le_tc/kernel/tc_mem_leak_checker.c",
    "build/configs/qemu/tc_1m/defconfig",
    "os/kernel/debug/Make.defs",
    "os/kernel/debug/mem_leak_checker_core.c",
    "os/kernel/debug/mem_leak_checker_core.h",
    "os/kernel/debug/mem_leak_checker_core_internal.h",
    "os/kernel/debug/mem_leak_checker_index.c",
    "os/kernel/debug/tests/test_mem_leak_checker_core.c",
    "tools/mem_leak_checker_contracts/task-1.json",
    "tools/mem_leak_checker_goldens/task-1-legacy.txt",
    "tools/mem_leak_checker_preflight.py",
    "tools/mem_leak_checker_qa.sh",
    "tools/mem_leak_checker_qa_core.py",
    "tools/mem_leak_checker_qemu_qa.py",
    "tools/mem_leak_checker_scenarios/task-1.json",
    "tools/mem_leak_checker_scenarios/task-3.json",
    "tools/test_mem_leak_checker_core.sh",
}
PARSER_CASES: Final = {
    "timeout",
    "malformed",
    "incomplete-with-verdict",
    "missing-second-prompt",
    "bad-red-linkage",
    "pipeline-failure",
    "missing-root",
}
SELF_TEST_CASES: Final = (
    "timeout",
    "malformed",
    "incomplete-with-verdict",
    "missing-second-prompt",
    "bad-red-linkage",
    "pipeline-failure",
    "missing-root",
    "preflight-context-create-failure",
    "preflight-context-fsync-failure",
    "preflight-worktree-partial",
    "preflight-evidence-dir-failure",
    "preflight-identity-failure",
    "preflight-concurrent-loser",
    "preflight-ancestor-symlink",
    "preflight-ancestor-replacement",
    "preflight-existing-context-refusal",
    "preflight-owned-child-cleanup",
)
ResultT = TypeVar("ResultT")


@dataclass(frozen=True, slots=True)
class RunnerContext:
    root: Path
    evidence: Path
    baseline_sha: str
    head_sha: str
    branch: str
    work_id: str
    session_id: str


@dataclass(frozen=True, slots=True)
class Task3Context:
    root: Path
    main_root: Path
    todo_root: Path
    evidence: Path
    baseline_sha: str
    todo_head: str
    main_head: str
    mode: str


def repository_root() -> Path:
    return Path(__file__).resolve().parent.parent


def _git(root: Path, *arguments: str) -> str:
    return subprocess.run(
        ["git", *arguments], cwd=root, text=True, capture_output=True, check=True
    ).stdout.strip()


def _open_directory_chain(path: Path) -> int:
    if not path.is_absolute():
        raise QaError("trusted directory must be absolute")
    flags = os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open("/", flags)
    try:
        for component in path.parts[1:]:
            next_descriptor = os.open(component, flags, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = next_descriptor
    except BaseException:  # noqa: BROAD_EXCEPT_OK -- descriptor ownership must unwind.
        os.close(descriptor)
        raise
    return descriptor


def _open_relative_directory(root: Path, relative: str) -> int:
    descriptor = _open_directory_chain(root)
    if relative == ".":
        return descriptor
    flags = os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0)
    try:
        for component in Path(relative).parts:
            if component in {"", ".", ".."}:
                raise QaError("unsafe trusted directory component")
            next_descriptor = os.open(component, flags, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = next_descriptor
    except BaseException:  # noqa: BROAD_EXCEPT_OK -- descriptor ownership must unwind.
        os.close(descriptor)
        raise
    return descriptor


def _read_relative_file(root: Path, relative: str) -> bytes:
    parts = Path(relative).parts
    if not parts or any(component in {"", ".", ".."} for component in parts):
        raise QaError("unsafe trusted file path")
    parent = str(Path(*parts[:-1])) if len(parts) > 1 else "."
    directory = _open_relative_directory(root, parent)
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(parts[-1], flags, dir_fd=directory)
        try:
            opened = os.fstat(descriptor)
            if not stat.S_ISREG(opened.st_mode):
                raise QaError("trusted path is not a regular file")
            chunks: list[bytes] = []
            total = 0
            while True:
                chunk = os.read(descriptor, 65536)
                if not chunk:
                    break
                total += len(chunk)
                if total > 4 * 1024 * 1024:
                    raise QaError("trusted file exceeds size limit")
                chunks.append(chunk)
            named = os.stat(parts[-1], dir_fd=directory, follow_symlinks=False)
            if (opened.st_dev, opened.st_ino) != (named.st_dev, named.st_ino):
                raise QaError("trusted file identity drift")
            return b"".join(chunks)
        finally:
            os.close(descriptor)
    finally:
        os.close(directory)


def _worktree_records(root: Path) -> tuple[dict[str, str], ...]:
    records: list[dict[str, str]] = []
    output = _git(root, "worktree", "list", "--porcelain")
    for block in output.split("\n\n"):
        record: dict[str, str] = {}
        for line in block.splitlines():
            key, _, value = line.partition(" ")
            record[key] = value
        records.append(record)
    return tuple(records)


def _validate_task3_boulder(value: JsonValue, main_root: Path) -> None:
    expected_work: dict[str, JsonValue] = {
        "work_id": WORK_ID,
        "active_plan": PLAN,
        "plan_name": "mem-leak-checker-hardening",
        "session_ids": [SESSION],
        "status": "active",
        "worktree_path": str(main_root),
    }
    expected: dict[str, JsonValue] = {
        "schema_version": 2,
        "active_work_id": WORK_ID,
        "works": {WORK_ID: expected_work},
    }
    if value != expected:
        raise QaError("Boulder v2 active context mismatch")


def _task3_context_mode(
    root: Path, main_worktree: Path, todo_worktree: Path, branch: str
) -> str:
    if root == todo_worktree and branch == TASK3_BRANCH and root != main_worktree:
        return "development"
    if root == main_worktree and branch == BRANCH and root != todo_worktree:
        return "receiving"
    raise QaError("Task3 worktree path/branch mode mismatch")


def _load_task3_context() -> Task3Context:
    root = repository_root().resolve()
    records = _worktree_records(root)
    main = tuple(
        item for item in records if item.get("branch") == f"refs/heads/{BRANCH}"
    )
    todo = tuple(
        item for item in records if item.get("branch") == f"refs/heads/{TASK3_BRANCH}"
    )
    if len(main) != 1 or len(todo) != 1:
        raise QaError("exact main/Todo3 worktree registration mismatch")
    main_root = Path(main[0]["worktree"]).resolve()
    todo_root = Path(todo[0]["worktree"]).resolve()
    branch = _git(root, "branch", "--show-current")
    mode = _task3_context_mode(root, main_root, todo_root, branch)
    main_descriptor = _open_directory_chain(main_root)
    todo_descriptor = _open_directory_chain(todo_root)
    os.close(main_descriptor)
    os.close(todo_descriptor)
    try:
        boulder: JsonValue = json.loads(
            _read_relative_file(main_root, ".omo/boulder.json")
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise QaError("invalid descriptor-bound Boulder v2 context") from error
    _validate_task3_boulder(boulder, main_root)
    _read_relative_file(main_root, PLAN)
    baseline = _read_relative_file(
        main_root, ".omo/start-work/artifacts/task-1-executor/baseline.sha"
    ).decode("ascii").strip()
    main_head = _git(main_root, "rev-parse", "HEAD")
    todo_registered_head = _git(todo_root, "rev-parse", "HEAD")
    source_head = _git(root, "rev-parse", "HEAD")
    if baseline != BASELINE or _git(main_root, "rev-parse", BASELINE) != BASELINE:
        raise QaError("durable baseline mismatch")
    if _git(main_root, "branch", "--show-current") != BRANCH or main[0].get("HEAD") != main_head:
        raise QaError("active main worktree branch/HEAD mismatch")
    if (
        _git(todo_root, "branch", "--show-current") != TASK3_BRANCH
        or todo[0].get("HEAD") != todo_registered_head
    ):
        raise QaError("Todo3 worktree branch/HEAD mismatch")
    if _git(root, "rev-parse", TASK3_BASE) != TASK3_BASE:
        raise QaError("Todo3 base commit missing")
    if _git(todo_root, "merge-base", TASK3_BASE, todo_registered_head) != TASK3_BASE:
        raise QaError("Todo3 branch is not rooted at the required base")
    subprocess.run(
        ["git", "merge-base", "--is-ancestor", BASELINE, main_head],
        cwd=main_root,
        check=True,
    )
    if subprocess.run(
        ["git", "merge-base", "--is-ancestor", TASK3_BASE, source_head],
        cwd=root,
        check=False,
    ).returncode != 0:
        raise QaError("Task3 source is not an integrated descendant")
    evidence = main_root / TASK3_EVIDENCE_RELATIVE
    evidence_descriptor = _open_relative_directory(main_root, TASK3_EVIDENCE_RELATIVE)
    os.close(evidence_descriptor)
    context = Task3Context(
        root, main_root, todo_root, evidence, baseline, source_head, main_head, mode
    )
    if mode == "receiving":
        _validate_task3_receiving_source(context)
    return context


def _task3_identity(context: Task3Context) -> dict[str, JsonValue]:
    if context.mode != "development":
        raise QaError("development identity requested in receiving mode")
    entries: list[dict[str, JsonValue]] = []
    digest = hashlib.sha256()
    for relative in TASK3_FIXTURE_PATHS:
        data = _read_relative_file(context.root, relative)
        file_hash = hashlib.sha256(data).hexdigest()
        entries.append({"path": relative, "sha256": file_hash})
        digest.update(f"{file_hash}  {relative}\n".encode())
    patch = subprocess.run(
        ["git", "diff", "HEAD", "--binary", "--", *TASK3_FIXTURE_PATHS],
        cwd=context.root,
        capture_output=True,
        check=True,
    ).stdout
    full_patch = subprocess.run(
        ["git", "diff", "HEAD", "--binary"],
        cwd=context.root,
        capture_output=True,
        check=True,
    ).stdout
    return {
        "baseline_sha": context.baseline_sha,
        "todo_base_sha": TASK3_BASE,
        "current_head": context.todo_head,
        "main_head": context.main_head,
        "main_worktree": str(context.main_root),
        "main_branch": BRANCH,
        "work_id": WORK_ID,
        "session_id": SESSION,
        "active_plan": PLAN,
        "todo_worktree": str(context.root),
        "todo_branch": TASK3_BRANCH,
        "fixture_digest": digest.hexdigest(),
        "fixture_files": entries,
        "fixture_patch_sha256": hashlib.sha256(patch).hexdigest(),
        "full_patch_sha256": hashlib.sha256(full_patch).hexdigest(),
        "staged_write_tree": _git(context.root, "write-tree"),
    }


def _publish_task3_receipt(
    main_root: Path, evidence: Path, name: str, value: dict[str, JsonValue]
) -> str:
    _wait_for_index_lock(main_root)
    publication = publication_record(evidence, force_weak=True)
    expected = canonical_bytes({**value, "publication": publication})
    relative = f"{TASK3_EVIDENCE_RELATIVE}/{name}"
    try:
        existing = _read_relative_file(main_root, relative)
    except FileNotFoundError:
        publish_json(evidence / name, value, force_weak=True)
        existing = _read_relative_file(main_root, relative)
    if existing != expected:
        raise QaError(f"existing Task3 receipt identity differs: {name}")
    return hashlib.sha256(existing).hexdigest()


def _wait_for_index_lock(root: Path) -> None:
    lock = Path(_git(root, "rev-parse", "--git-path", "index.lock"))
    if not lock.is_absolute():
        lock = root / lock
    for _ in range(200):
        if not lock.exists():
            return
        time.sleep(0.05)
    raise QaError(f"git index lock did not clear: {lock}")


def _stable_task3_text(value: str, context: Task3Context, temporary: Path) -> str:
    stable = value.replace(str(temporary), "$TMP").replace(
        str(context.root), "$SOURCE_ROOT"
    )
    return re.sub(r"test_mem_leak_checker_core-[A-Za-z0-9]+\.o", "task3-red-proof.o", stable)


def _parse_boulder(path: Path, root: Path) -> tuple[str, str]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise QaError("invalid Boulder JSON") from error
    if not isinstance(raw, dict) or set(raw) != {"schema_version", "active_work_id", "works"}:
        raise QaError("Boulder schema v2 top-level mismatch")
    if raw["schema_version"] != 2 or raw["active_work_id"] != WORK_ID:
        raise QaError("Boulder schema or active work mismatch")
    works = raw["works"]
    if not isinstance(works, dict) or set(works) != {WORK_ID}:
        raise QaError("Boulder active work set mismatch")
    work = works[WORK_ID]
    expected = {
        "work_id": WORK_ID,
        "active_plan": PLAN,
        "plan_name": "mem-leak-checker-hardening",
        "session_ids": [SESSION],
        "status": "active",
        "worktree_path": str(root),
    }
    if work != expected:
        raise QaError("Boulder active work fields mismatch")
    plan = (root / work["active_plan"]).resolve()
    if plan != (root / PLAN).resolve() or not plan.is_file():
        raise QaError("Boulder active plan mismatch")
    return WORK_ID, SESSION


def _validate_known_todo_diff(root: Path) -> None:
    status = subprocess.run(
        ["git", "status", "--porcelain=v1", "-z", "--untracked-files=all"],
        cwd=root,
        capture_output=True,
        check=True,
    ).stdout
    for entry in status.split(b"\0"):
        if not entry:
            continue
        path = entry[3:].decode("utf-8", errors="strict")
        if path.startswith(".omo/") or path in KNOWN_TODO_PATHS:
            continue
        raise QaError(f"unexpected worktree change outside Todo 1: {path}")


def load_context() -> RunnerContext:
    root = repository_root().resolve()
    head = validate_root(root)
    work_id, session = _parse_boulder(root / ".omo/boulder.json", root)
    baseline_path = root / EVIDENCE_RELATIVE / "baseline.sha"
    try:
        baseline = baseline_path.read_text(encoding="ascii").strip()
    except (OSError, UnicodeDecodeError) as error:
        raise QaError("missing canonical baseline receipt") from error
    if baseline != BASELINE or _git(root, "rev-parse", BASELINE) != BASELINE:
        raise QaError("baseline mismatch")
    if subprocess.run(
        ["git", "merge-base", "--is-ancestor", baseline, head], cwd=root, check=False
    ).returncode != 0:
        raise QaError("current HEAD is outside the canonical baseline history")
    branch = _git(root, "branch", "--show-current")
    if branch != BRANCH:
        raise QaError("current branch mismatch")
    _validate_known_todo_diff(root)
    evidence = (root / EVIDENCE_RELATIVE).resolve()
    if evidence != root / EVIDENCE_RELATIVE or not evidence.is_dir():
        raise QaError("canonical evidence directory mismatch")
    return RunnerContext(root, evidence, baseline, head, branch, work_id, session)


def _expect_rejection(action: Callable[[], ResultT], label: str) -> None:
    try:
        action()
    except (OSError, QaError, json.JSONDecodeError, subprocess.CalledProcessError):
        return
    raise QaError(f"negative case was accepted: {label}")


def _run_cli_expect_failure(arguments: list[str]) -> None:
    result = subprocess.run([sys.executable, __file__, *arguments], capture_output=True)
    if result.returncode == 0:
        raise QaError(f"CLI negative boundary was accepted: {' '.join(arguments)}")


def _records() -> str:
    return (
        "MLC_QA fixture=mlc_bootstrap status=PASS baseline_sha=" + BASELINE + "\n"
        "MLC_QA fixture=mlc_characterization self=hidden cycle=hidden "
        "chain_only_head=reported gating=false\n"
    )


def _parser_case(case: str, context: RunnerContext) -> None:
    fixtures = ("mlc_bootstrap", "mlc_characterization")
    records = _records()
    summary = "########## Kernel TC End [PASS : 7, FAIL : 0] ##########\n"
    if case == "timeout":
        with tempfile.TemporaryDirectory(prefix="mlc-timeout-", dir=context.evidence) as directory:
            log = Path(directory) / "child.log"
            pid_file = Path(directory) / "pids"
            program = (
                "import os,subprocess,sys,time; "
                "child=subprocess.Popen(['sleep','30']); "
                "open(sys.argv[1],'w').write(f'{os.getpid()} {child.pid}\\n'); "
                "time.sleep(30)"
            )
            _expect_rejection(
                lambda: run_checked(
                    [sys.executable, "-c", program, str(pid_file)],
                    context.root,
                    log,
                    timeout=0.2,
                ),
                case,
            )
            pids = [int(item) for item in pid_file.read_text(encoding="ascii").split()]
            for pid in pids:
                state = subprocess.run(
                    ["ps", "-o", "stat=", "-p", str(pid)], text=True, capture_output=True, check=False
                ).stdout.strip()
                if state and not state.startswith("Z"):
                    raise QaError(f"timeout left live process-group member: {pid} {state}")
        _leader_exited_process_group_case(context)
        _interrupt_waiting_process_group_case(context)
        _expect_rejection(lambda: parse_completed_transcript(records + summary, fixtures), case)
    elif case == "malformed":
        with tempfile.TemporaryDirectory(prefix="mlc-malformed-", dir=context.evidence) as directory:
            malformed = Path(directory) / "bad.json"
            malformed.write_text("{not-json", encoding="utf-8")
            _run_cli_expect_failure(["validate-receipt", "--path", str(malformed)])
            _run_cli_expect_failure(["validate-scenario", "--path", str(malformed)])
            _run_cli_expect_failure(
                ["validate-boulder", "--path", str(malformed), "--root", str(context.root)]
            )
            boulder = _load_json_object(context.root / ".omo/boulder.json")
            missing = Path(directory) / "missing-work.json"
            missing.write_bytes(canonical_bytes({"schema_version": 2}))
            _run_cli_expect_failure(
                ["validate-boulder", "--path", str(missing), "--root", str(context.root)]
            )
            mismatched = Path(directory) / "wrong-worktree.json"
            works = boulder["works"]
            if not isinstance(works, dict) or not isinstance(works.get(WORK_ID), dict):
                raise QaError("canonical Boulder work shape changed during self-test")
            wrong_work = {**works[WORK_ID], "worktree_path": str(context.root.parent)}
            mismatched.write_bytes(canonical_bytes({**boulder, "works": {WORK_ID: wrong_work}}))
            _run_cli_expect_failure(
                ["validate-boulder", "--path", str(mismatched), "--root", str(context.root)]
            )
    elif case == "incomplete-with-verdict":
        bad = records + "MLC_INCOMPLETE reason=roots\nLEAK   | 0x1 | 1 | 0x2 | 3\n" + summary + PROMPT
        _expect_rejection(lambda: parse_completed_transcript(bad, fixtures), case)
    elif case == "missing-second-prompt":
        _expect_rejection(lambda: parse_completed_transcript(PROMPT + records + summary, fixtures), case)
    elif case == "bad-red-linkage":
        with tempfile.TemporaryDirectory(prefix="mlc-receipt-", dir=context.evidence) as directory:
            receipt = Path(directory) / "red.json"
            receipt.write_bytes(canonical_bytes({"exit": 86, "fixture_digest": ""}))
            _run_cli_expect_failure(["validate-receipt", "--path", str(receipt)])
    elif case == "pipeline-failure":
        result = subprocess.run(
            [str(context.root / "tools/mem_leak_checker_qa.sh"), "pipeline-probe", "19"],
            cwd=context.root,
            capture_output=True,
        )
        if result.returncode != 19:
            raise QaError(f"pipeline boundary lost exit status: {result.returncode}")
    elif case == "missing-root":
        with tempfile.TemporaryDirectory(prefix="mlc-missing-root-", dir=context.evidence) as directory:
            _expect_rejection(lambda: validate_root(Path(directory).resolve()), case)
    else:
        raise QaError(f"unknown parser self-test: {case}")


def _validate_red(value: JsonValue) -> None:
    if not isinstance(value, dict):
        raise QaError("RED receipt must be an object")
    required = {"exit", "fixture_digest", "tree", "patch", "baseline_sha", "head_sha", "command"}
    if value.get("exit") != 86 or any(not value.get(key) for key in required - {"exit"}):
        raise QaError("invalid RED linkage")


def _load_json_object(path: Path) -> dict[str, JsonValue]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise QaError(f"malformed JSON: {path}") from error
    if not isinstance(value, dict):
        raise QaError(f"JSON object required: {path}")
    return value


def _validate_task_1_scenario(value: dict[str, JsonValue]) -> None:
    if set(value) != {"schema", "task", "red_exempt", "happy", "failure"}:
        raise QaError("task-1 scenario keys mismatch")
    if value["schema"] != 1 or value["task"] != 1 or value["red_exempt"] is not True:
        raise QaError("task-1 scenario identity mismatch")
    happy = value["happy"]
    failure = value["failure"]
    if not isinstance(happy, list) or len(happy) != 1 or not isinstance(failure, list) or len(failure) != 1:
        raise QaError("task-1 scenario cardinality mismatch")
    happy_item = happy[0]
    failure_item = failure[0]
    if not isinstance(happy_item, dict) or not isinstance(failure_item, dict):
        raise QaError("task-1 scenario entry must be an object")
    if happy_item.get("expected_status") != "deferred_unexecuted_baseline_link_failure":
        raise QaError("QEMU scenario must be explicitly deferred")
    if happy_item.get("command") != (
        "tools/mem_leak_checker_qa.sh qemu --task 1 --fixtures mlc_bootstrap,mlc_characterization"
    ):
        raise QaError("QEMU scenario command drift")
    expected_records = [
        "MLC_QA fixture=mlc_bootstrap status=PASS",
        "MLC_QA fixture=mlc_characterization self=hidden cycle=hidden "
        "chain_only_head=reported gating=false",
    ]
    if happy_item.get("expected_exit") is not None or happy_item.get("expected_records") != expected_records:
        raise QaError("deferred QEMU expectations drift")
    expected_failure_command = (
        "tools/mem_leak_checker_qa.sh self-test --task 1 --cases "
        + ",".join(SELF_TEST_CASES)
        + ' --output-json "$(tools/mem_leak_checker_qa.sh context '
        '--print-evidence-dir)/task-1/preflight-self-test.json"'
    )
    if failure_item.get("command") != expected_failure_command:
        raise QaError("self-test scenario command drift")
    if failure_item.get("expected_exit") != 0 or failure_item.get("expected_record") != "all cases status=PASS":
        raise QaError("self-test scenario expectation drift")


def _validate_task_3_scenario(value: dict[str, JsonValue]) -> None:
    if set(value) != {"schema", "task", "red_exempt", "red", "happy", "failure"}:
        raise QaError("task-3 scenario keys mismatch")
    if value["schema"] != 1 or value["task"] != 3 or value["red_exempt"] is not False:
        raise QaError("task-3 scenario identity mismatch")
    red = value["red"]
    happy = value["happy"]
    failure = value["failure"]
    if (
        not isinstance(red, list)
        or len(red) != 1
        or not isinstance(happy, list)
        or len(happy) != 2
        or not isinstance(failure, list)
        or len(failure) != 2
    ):
        raise QaError("task-3 scenario cardinality mismatch")
    red_item = red[0]
    host_item, qemu_item = happy
    failure_item, negative_item = failure
    if (
        not isinstance(red_item, dict)
        or not isinstance(host_item, dict)
        or not isinstance(qemu_item, dict)
        or not isinstance(failure_item, dict)
        or not isinstance(negative_item, dict)
    ):
        raise QaError("task-3 scenario entry must be an object")
    expected_red: dict[str, JsonValue] = {
        "command": (
            "tools/mem_leak_checker_qa.sh red --task 3 --config qemu/tc_1m "
            "--fixtures mlc_scanner_index,mlc_zero_exact_precedence"
        ),
        "expected_exit": 86,
        "fixtures": "mlc_scanner_index,mlc_zero_exact_precedence",
        "expected_record_prefix": (
            "MLC_QA_RED task=3 fixtures=mlc_scanner_index,mlc_zero_exact_precedence "
            "status=evidence_bound_expected_failure exit=86"
        ),
        "required_fields": ["receipt_sha256", "proof_exit", "proof_log_sha256"],
    }
    if red_item != expected_red:
        raise QaError("task-3 RED scenario drift")
    expected_host: dict[str, JsonValue] = {
        "command": (
            "tools/test_mem_leak_checker_core.sh --fixtures "
            "mlc_scanner_index,mlc_zero_exact_precedence"
        ),
        "expected_exit": 0,
        "expected_records": [
            "MLC_HOST fixture=mlc_scanner_index status=PASS",
            "MLC_HOST fixture=mlc_zero_exact_precedence status=PASS",
        ],
    }
    if host_item != expected_host:
        raise QaError("task-3 host happy scenario drift")
    expected_qemu: dict[str, JsonValue] = {
        "command": (
            "tools/mem_leak_checker_qa.sh qemu --task 3 --fixtures "
            "mlc_scanner_index,mlc_zero_exact_precedence"
        ),
        "expected_exit": None,
        "expected_status": "deferred_unexecuted_baseline_link_failure",
        "expected_record_prefix": (
            "MLC_QA_QEMU task=3 fixtures=mlc_scanner_index,mlc_zero_exact_precedence "
            "status=deferred_unexecuted_baseline_link_failure"
        ),
        "required_fields": ["red_linkage=verified", "receipt_sha256"],
    }
    if qemu_item != expected_qemu:
        raise QaError("task-3 deferred QEMU scenario drift")
    expected_failure: dict[str, JsonValue] = {
        "command": (
            "tools/mem_leak_checker_qa.sh qemu --task 3 --fixture "
            "mlc_scanner_invalid_ranges"
        ),
        "expected_exit": None,
        "expected_status": "deferred_unexecuted_baseline_link_failure",
        "expected_record_prefix": (
            "MLC_QA_QEMU task=3 fixtures=mlc_scanner_invalid_ranges "
            "status=deferred_unexecuted_baseline_link_failure"
        ),
        "required_fields": ["red_linkage=verified", "receipt_sha256"],
    }
    if failure_item != expected_failure:
        raise QaError("task-3 host failure scenario drift")
    expected_negative: dict[str, JsonValue] = {
        "command": "tools/mem_leak_checker_qa.sh receipt-negative --task 3",
        "expected_exit": 0,
        "expected_record_prefix": (
            "MLC_QA_RECEIPT_NEGATIVE task=3 "
            "cases=symlink,boulder,wrong_branch,baseline_mismatch,cross_fixture,"
            "cross_command status=PASS"
        ),
        "required_fields": ["receipt_sha256"],
    }
    if negative_item != expected_negative:
        raise QaError("task-3 linkage-negative scenario drift")


def _validate_scenario(value: dict[str, JsonValue]) -> None:
    task = value.get("task")
    if task == 1:
        _validate_task_1_scenario(value)
        return
    if task == 3:
        _validate_task_3_scenario(value)
        return
    raise QaError(f"unsupported scenario task: {task}")


def run_self_tests(cases: tuple[str, ...], output: Path, context: RunnerContext) -> None:
    if cases != SELF_TEST_CASES:
        raise QaError("Todo 1 self-test case list or order mismatch")
    try:
        output.relative_to(context.evidence)
    except ValueError as error:
        raise QaError("self-test output must remain in canonical evidence") from error
    if output.name != "preflight-self-test.json":
        raise QaError("self-test output name mismatch")
    results: list[dict[str, JsonValue]] = []
    for case in cases:
        started = time.monotonic_ns()
        if case in PARSER_CASES:
            _parser_case(case, context)
        elif case.startswith("preflight-"):
            run_preflight_case(case)
        else:
            raise QaError(f"unknown self-test case: {case}")
        results.append({"case": case, "status": "PASS", "duration_ns": time.monotonic_ns() - started})
    _publication_boundary_tests(context)
    publish_json(
        output,
        {
            "schema": 1,
            "task": 1,
            "baseline_sha": context.baseline_sha,
            "receiving_sha": context.head_sha,
            "work_id": context.work_id,
            "session_id": context.session_id,
            "branch": context.branch,
            "command": "self-test",
            "exit": 0,
            "cases": results,
        },
    )


def _publication_boundary_tests(context: RunnerContext) -> None:
    with tempfile.TemporaryDirectory(prefix="mlc-publication-", dir=context.evidence) as directory:
        parent = Path(directory)

        def short_writer(fd: int, payload: bytes) -> int:
            return os.write(fd, payload[:1])

        short_path = parent / "short.json"
        publish_json(short_path, {"result": "complete"}, writer=short_writer, force_weak=True)
        if _load_json_object(short_path).get("result") != "complete":
            raise QaError("short-write publication was incomplete")

        writes = 0

        def interrupted_writer(fd: int, payload: bytes) -> int:
            nonlocal writes
            writes += 1
            if writes == 1:
                return os.write(fd, payload[:1])
            raise KeyboardInterrupt

        interrupted_path = parent / "interrupted.json"
        try:
            publish_json(
                interrupted_path,
                {"result": "must-not-publish"},
                writer=interrupted_writer,
                force_weak=True,
            )
        except KeyboardInterrupt:
            interrupted = True
        else:
            raise QaError("interrupted publication unexpectedly succeeded")
        if interrupted_path.exists():
            raise QaError("interrupted weak publication left a partial final inode")

        replacement_path = parent / "replacement.json"
        displaced_path = parent / "replacement-owned-partial"
        replacement_writes = 0

        def replacement_writer(fd: int, payload: bytes) -> int:
            nonlocal replacement_writes
            replacement_writes += 1
            if replacement_writes == 1:
                return os.write(fd, payload[:1])
            replacement_path.rename(displaced_path)
            replacement_path.write_text("foreign\n", encoding="utf-8")
            raise KeyboardInterrupt

        try:
            publish_json(
                replacement_path,
                {"result": "must-not-remove-replacement"},
                writer=replacement_writer,
                force_weak=True,
            )
        except KeyboardInterrupt:
            replacement_interrupted = True
        else:
            raise QaError("replacement-race publication unexpectedly succeeded")
        if replacement_path.read_text(encoding="utf-8") != "foreign\n":
            raise QaError("weak rollback removed or changed a replacement inode")
        displaced_path.unlink()

        collision = parent / "collision.json"
        collision.write_text("foreign\n", encoding="utf-8")
        _expect_rejection(
            lambda: publish_json(collision, {"result": "owner"}, force_weak=True),
            "publication-collision",
        )
        if collision.read_text(encoding="utf-8") != "foreign\n":
            raise QaError("publication collision changed foreign content")


def _leader_exited_process_group_case(context: RunnerContext) -> None:
    with tempfile.TemporaryDirectory(prefix="mlc-exited-leader-", dir=context.evidence) as directory:
        pid_file = Path(directory) / "descendants"
        child_program = "\n".join(
            (
                "import os, signal, subprocess, sys, time",
                "grandchild = subprocess.Popen(['sleep', '30'])",
                "open(sys.argv[1], 'w').write(f'{os.getpid()} {grandchild.pid}\\n')",
                "def stop(_number, _frame):",
                "    grandchild.terminate()",
                "    grandchild.wait(timeout=2)",
                "    raise SystemExit(0)",
                "signal.signal(signal.SIGTERM, stop)",
                "time.sleep(30)",
            )
        )
        leader_program = "\n".join(
            (
                "import pathlib, subprocess, sys, time",
                f"subprocess.Popen([sys.executable, '-c', {child_program!r}, sys.argv[1]])",
                "path = pathlib.Path(sys.argv[1])",
                "deadline = time.monotonic() + 5",
                "while not path.exists() and time.monotonic() < deadline:",
                "    time.sleep(0.01)",
                "raise SystemExit(0 if path.exists() else 2)",
            )
        )
        process = subprocess.Popen(
            [sys.executable, "-c", leader_program, str(pid_file)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        pgid = process.pid
        try:
            if process.wait(timeout=5) != 0:
                raise QaError("leader-exited fixture failed before cleanup")
            pids = [int(item) for item in pid_file.read_text(encoding="ascii").split()]
            terminate_process_group(process, pgid, grace=1.0)
            deadline = time.monotonic() + 2.0
            states: dict[int, str] = {}
            while time.monotonic() < deadline:
                states = {
                    pid: subprocess.run(
                        ["ps", "-o", "stat=", "-p", str(pid)],
                        text=True,
                        capture_output=True,
                        check=False,
                    ).stdout.strip()
                    for pid in pids
                }
                if all(not state for state in states.values()):
                    break
                time.sleep(0.01)
            if any(states.values()):
                raise QaError(f"leader-exited cleanup left descendants: {states}")
        finally:
            try:
                os.killpg(pgid, signal.SIGKILL)
            except ProcessLookupError:
                group_absent = True


def _interrupt_waiting_process_group_case(context: RunnerContext) -> None:
    with tempfile.TemporaryDirectory(prefix="mlc-interrupt-", dir=context.evidence) as directory:
        fixture = Path(directory)
        log = fixture / "command.log"
        pid_file = fixture / "pids"
        child_program = (
            "import os,subprocess,sys,time; "
            "child=subprocess.Popen(['sleep','30']); "
            "open(sys.argv[1],'w').write(f'{os.getpid()} {child.pid}\\n'); "
            "time.sleep(30)"
        )
        runner_program = "\n".join(
            (
                "import pathlib, sys",
                f"sys.path.insert(0, {str(context.root / 'tools')!r})",
                "from mem_leak_checker_qa_core import run_checked",
                "run_checked(",
                f"    [sys.executable, '-c', {child_program!r}, sys.argv[1]],",
                f"    pathlib.Path({str(context.root)!r}),",
                "    pathlib.Path(sys.argv[2]),",
                "    timeout=30.0,",
                ")",
            )
        )
        runner = subprocess.Popen(
            [sys.executable, "-c", runner_program, str(pid_file), str(log)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        command_pgid = -1
        try:
            deadline = time.monotonic() + 5.0
            while not pid_file.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            if not pid_file.exists():
                raise QaError("interrupt fixture did not reach run_checked wait")
            pids = [int(item) for item in pid_file.read_text(encoding="ascii").split()]
            command_pgid = pids[0]
            os.kill(runner.pid, signal.SIGINT)
            return_code = runner.wait(timeout=5.0)
            if return_code == 0:
                raise QaError("SIGINT boundary unexpectedly reported success")
            try:
                os.killpg(command_pgid, 0)
            except ProcessLookupError:
                group_absent = True
            else:
                raise QaError(f"SIGINT left owned process group: {command_pgid}")
            states = {
                pid: subprocess.run(
                    ["ps", "-o", "stat=", "-p", str(pid)],
                    text=True,
                    capture_output=True,
                    check=False,
                ).stdout.strip()
                for pid in pids
            }
            if any(state and not state.startswith("Z") for state in states.values()):
                raise QaError(f"SIGINT left live process-group members: {states}")
        finally:
            if runner.poll() is None:
                runner.kill()
                runner.wait(timeout=2.0)
            if command_pgid > 1 and command_pgid != os.getpgrp():
                try:
                    os.killpg(command_pgid, signal.SIGKILL)
                except ProcessLookupError:
                    group_absent = True


def qemu_command(args: argparse.Namespace, context: RunnerContext) -> None:
    binary = Path(args.binary).resolve()
    fixtures = tuple(item for item in args.fixtures.split(",") if item)
    result = run_qemu(binary, fixtures, args.timeout)
    attempt = context.evidence / f"task-{args.task}" / "qemu-attempt-1"
    attempt.mkdir(mode=0o700, parents=True, exist_ok=False)
    transcript = attempt / "transcript.txt"
    transcript.write_text(result.transcript, encoding="utf-8")
    publish_json(
        attempt / "qemu.json",
        {
            "schema": 1,
            "task": args.task,
            "baseline_sha": context.baseline_sha,
            "receiving_sha": context.head_sha,
            "exit": 0,
            "fixtures": list(fixtures),
            "records": list(result.records),
            "baseline_characterization": {"gating": False, "records": list(result.records)},
            "transcript": {"path": str(transcript), "sha256": sha256_path(transcript)},
        },
    )


def _task3_scenario(root: Path) -> dict[str, JsonValue]:
    try:
        value: JsonValue = json.loads(
            _read_relative_file(root, "tools/mem_leak_checker_scenarios/task-3.json")
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise QaError("invalid task-3 scenario JSON") from error
    if not isinstance(value, dict):
        raise QaError("task-3 scenario object required")
    _validate_task_3_scenario(value)
    return value


def _task3_red_command(scenario: dict[str, JsonValue]) -> tuple[str, str]:
    red = scenario["red"]
    if not isinstance(red, list) or not red or not isinstance(red[0], dict):
        raise QaError("task-3 RED scenario missing")
    command = red[0].get("command")
    fixtures = red[0].get("fixtures")
    if not isinstance(command, str) or not isinstance(fixtures, str):
        raise QaError("task-3 RED scenario fields invalid")
    return command, fixtures


def _validate_task3_red_document(
    document: dict[str, JsonValue],
    scenario: dict[str, JsonValue],
    identity: dict[str, JsonValue] | None = None,
) -> None:
    command, fixtures = _task3_red_command(scenario)
    if (
        document.get("schema") != 1
        or document.get("task") != 3
        or document.get("kind") != "development-red"
        or document.get("exit") != 86
        or document.get("command") != command
        or document.get("fixtures") != fixtures
    ):
        raise QaError("invalid task-3 RED receipt linkage")
    proof_exit = document.get("proof_exit")
    proof_log = document.get("proof_log")
    proof_command = document.get("proof_command")
    if (
        not isinstance(proof_exit, int)
        or proof_exit == 0
        or not isinstance(proof_log, str)
        or not isinstance(proof_command, list)
        or not all(isinstance(item, str) for item in proof_command)
        or document.get("proof_log_sha256")
        != hashlib.sha256(proof_log.encode()).hexdigest()
    ):
        raise QaError("invalid task-3 RED failing proof")
    if identity is not None:
        for key, value in identity.items():
            if document.get(key) != value:
                raise QaError(f"task-3 RED receipt drift: {key}")


def _validate_task3_receiving_source(context: Task3Context) -> None:
    relative = f"{TASK3_EVIDENCE_RELATIVE}/task-3-red.json"
    try:
        value: JsonValue = json.loads(_read_relative_file(context.main_root, relative))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise QaError("invalid Task3 receiving RED receipt") from error
    if not isinstance(value, dict):
        raise QaError("Task3 receiving RED receipt object required")
    _validate_task3_red_document(value, _task3_scenario(context.root))
    expected_identity: dict[str, JsonValue] = {
        "baseline_sha": BASELINE,
        "todo_base_sha": TASK3_BASE,
        "current_head": TASK3_BASE,
        "main_worktree": str(context.main_root),
        "main_branch": BRANCH,
        "work_id": WORK_ID,
        "session_id": SESSION,
        "active_plan": PLAN,
        "todo_worktree": str(context.todo_root),
        "todo_branch": TASK3_BRANCH,
    }
    for key, expected in expected_identity.items():
        if value.get(key) != expected:
            raise QaError(f"Task3 receiving RED identity drift: {key}")
    red_main_head = value.get("main_head")
    if not isinstance(red_main_head, str) or subprocess.run(
        ["git", "merge-base", "--is-ancestor", red_main_head, context.main_head],
        cwd=context.main_root,
        check=False,
    ).returncode != 0:
        raise QaError("Task3 receiving RED main history mismatch")
    fixture_files = value.get("fixture_files")
    if not isinstance(fixture_files, list) or len(fixture_files) != len(
        TASK3_FIXTURE_PATHS
    ):
        raise QaError("Task3 receiving owned blob set mismatch")
    observed_paths: list[str] = []
    for entry in fixture_files:
        if not isinstance(entry, dict):
            raise QaError("Task3 receiving owned blob entry invalid")
        path = entry.get("path")
        expected_hash = entry.get("sha256")
        if not isinstance(path, str) or not isinstance(expected_hash, str):
            raise QaError("Task3 receiving owned blob identity invalid")
        observed_paths.append(path)
        if hashlib.sha256(_read_relative_file(context.root, path)).hexdigest() != expected_hash:
            raise QaError(f"Task3 receiving owned blob drift: {path}")
    if tuple(observed_paths) != TASK3_FIXTURE_PATHS:
        raise QaError("Task3 receiving owned blob paths mismatch")
    qa_source = _read_relative_file(context.root, "tools/mem_leak_checker_qa.sh").decode()
    make_source = _read_relative_file(context.root, "os/kernel/debug/Make.defs").decode()
    if (
        "task2_receipt" not in qa_source
        or "mlc_alloc_bounds" not in qa_source
        or "task-3-deferred" not in qa_source
        or make_source.count("mem_leak_checker_core.c") != 1
        or make_source.count("mem_leak_checker_index.c") != 1
    ):
        raise QaError("Todo2 preservation or Task3 semantic registration mismatch")


def _read_task3_red(context: Task3Context) -> tuple[dict[str, JsonValue], str]:
    relative = f"{TASK3_EVIDENCE_RELATIVE}/task-3-red.json"
    payload = _read_relative_file(context.main_root, relative)
    try:
        value: JsonValue = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise QaError("invalid task-3 RED receipt JSON") from error
    if not isinstance(value, dict):
        raise QaError("task-3 RED receipt object required")
    identity = _task3_identity(context) if context.mode == "development" else None
    _validate_task3_red_document(value, _task3_scenario(context.root), identity)
    return value, hashlib.sha256(payload).hexdigest()


def task_3_red(args: argparse.Namespace) -> int:
    context = _load_task3_context()
    scenario = _task3_scenario(context.root)
    command, expected_fixtures = _task3_red_command(scenario)
    if args.fixtures != expected_fixtures:
        raise QaError("RED invocation does not match canonical task-3 scenario")
    _wait_for_index_lock(context.main_root)
    compiler = shutil.which("cc")
    if compiler is None:
        raise QaError("host C compiler is unavailable")
    with tempfile.TemporaryDirectory(prefix="mlc-task3-red-") as directory:
        temporary = Path(directory)
        binary = temporary / "missing-core-and-index"
        proof_command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            f"-I{context.root / 'os/kernel/debug'}",
            str(context.root / "os/kernel/debug/tests/test_mem_leak_checker_core.c"),
            "-o",
            str(binary),
        ]
        proof = subprocess.run(
            proof_command, cwd=context.root, text=True, capture_output=True, check=False
        )
        if proof.returncode == 0:
            raise QaError("task-3 RED missing-core/index proof unexpectedly linked")
        proof_log = _stable_task3_text(proof.stdout + proof.stderr, context, temporary)
        stable_command = [
            _stable_task3_text(item, context, temporary) for item in proof_command
        ]
    document: dict[str, JsonValue] = {
        "schema": 1,
        "task": 3,
        "kind": "development-red",
        "fixtures": expected_fixtures,
        "command": command,
        "exit": 86,
        "proof_command": stable_command,
        "proof_exit": proof.returncode,
        "proof_log": proof_log,
        "proof_log_sha256": hashlib.sha256(proof_log.encode()).hexdigest(),
        **_task3_identity(context),
    }
    receipt_hash = _publish_task3_receipt(
        context.main_root, context.evidence, "task-3-red.json", document
    )
    print(
        "MLC_QA_RED task=3 fixtures="
        f"{expected_fixtures} status=evidence_bound_expected_failure "
        f"exit=86 receipt_sha256={receipt_hash} proof_exit={proof.returncode} "
        f"proof_log_sha256={document['proof_log_sha256']}"
    )
    return 86


def _task3_fixture_tuple(selection: str) -> tuple[str, ...]:
    fixtures = tuple(selection.split(","))
    allowed = {
        "mlc_scanner_index",
        "mlc_zero_exact_precedence",
        "mlc_scanner_invalid_ranges",
    }
    if not fixtures or any(not item for item in fixtures) or len(set(fixtures)) != len(fixtures):
        raise QaError("malformed task-3 fixture selection")
    if any(item not in allowed for item in fixtures):
        raise QaError("unknown task-3 fixture")
    return fixtures


def _run_task3_host(root: Path, fixtures: tuple[str, ...]) -> tuple[str, ...]:
    result = subprocess.run(
        [str(root / "tools/test_mem_leak_checker_core.sh"), "--fixtures", ",".join(fixtures)],
        cwd=root,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise QaError(f"task-3 host fixtures failed: {result.stderr.strip()}")
    records = tuple(line for line in result.stdout.splitlines() if line)
    expected_records = {
        "mlc_scanner_index": "MLC_HOST fixture=mlc_scanner_index status=PASS",
        "mlc_zero_exact_precedence": (
            "MLC_HOST fixture=mlc_zero_exact_precedence status=PASS"
        ),
        "mlc_scanner_invalid_ranges": (
            "MLC_HOST fixture=mlc_scanner_invalid_ranges status=PASS "
            "matches=atomic canaries=intact"
        ),
    }
    expected = tuple(expected_records[fixture] for fixture in fixtures)
    if records != expected:
        raise QaError("task-3 host records mismatch")
    return records


def task_3_deferred(args: argparse.Namespace, *, emit: bool = True) -> dict[str, JsonValue]:
    context = _load_task3_context()
    fixtures = _task3_fixture_tuple(args.fixtures)
    _, red_hash = _read_task3_red(context)
    if context.mode == "receiving":
        receiving_tree = _git(context.root, "write-tree")
        baseline_patch = hashlib.sha256(
            subprocess.run(
                ["git", "diff", f"{BASELINE}..{context.todo_head}", "--binary"],
                cwd=context.root,
                capture_output=True,
                check=True,
            ).stdout
        ).hexdigest()
        result = _post_task3_deferred(
            context.root,
            context.evidence,
            red_hash,
            fixtures,
            args.selector,
            context.todo_head,
            receiving_tree,
            baseline_patch,
        )
        if emit:
            records = result.get("records")
            if not isinstance(records, list) or not all(
                isinstance(record, str) for record in records
            ):
                raise QaError("integrated Task3 deferred records invalid")
            for record in records:
                print(record)
        return result
    host_records = _run_task3_host(context.root, fixtures)
    identity = _task3_identity(context)
    patch_hash = identity["full_patch_sha256"]
    if not isinstance(patch_hash, str):
        raise QaError("task-3 patch identity type mismatch")
    fixture_hash = hashlib.sha256(",".join(fixtures).encode()).hexdigest()[:16]
    name = f"qemu-deferred-{args.selector}-{context.todo_head}-{patch_hash}-{fixture_hash}.json"
    receipt_hash = _publish_task3_receipt(
        context.main_root,
        context.evidence,
        name,
        {
            "schema": 1,
            "task": 3,
            "receiving_sha": context.todo_head,
            "branch": TASK3_BRANCH,
            "identity": identity,
            "red_receipt_sha256": red_hash,
            "scenario_sha256": hashlib.sha256(
                _read_relative_file(context.root, "tools/mem_leak_checker_scenarios/task-3.json")
            ).hexdigest(),
            "fixtures": list(fixtures),
            "selector": args.selector,
            "status": "deferred_unexecuted_baseline_link_failure",
            "executed": False,
            "exit": None,
        },
    )
    record = (
        f"MLC_QA_QEMU task=3 fixtures={','.join(fixtures)} "
        "status=deferred_unexecuted_baseline_link_failure red_linkage=verified "
        f"receipt_sha256={receipt_hash}"
    )
    if emit:
        for host_record in host_records:
            print(host_record)
        print(record)
    return {
        "command": (
            f"tools/mem_leak_checker_qa.sh qemu --task 3 --{args.selector} "
            f"{','.join(fixtures)}"
        ),
        "exit": 0,
        "records": [*host_records, record],
        "artifacts": {name: receipt_hash},
    }


def task_3_receipt_negative(*, emit: bool = True) -> dict[str, JsonValue]:
    context = _load_task3_context()
    document, receipt_hash = _read_task3_red(context)
    scenario = _task3_scenario(context.root)
    identity = (
        _task3_identity(context)
        if context.mode == "development"
        else {
            key: document[key]
            for key in (
                "baseline_sha",
                "todo_base_sha",
                "current_head",
                "main_head",
                "main_worktree",
                "main_branch",
                "work_id",
                "session_id",
                "active_plan",
                "todo_worktree",
                "todo_branch",
                "fixture_digest",
                "fixture_files",
                "fixture_patch_sha256",
                "full_patch_sha256",
                "staged_write_tree",
            )
        }
    )
    mutations: list[dict[str, JsonValue]] = []
    for key, value in (
        ("fixtures", "mlc_zero_exact_precedence,mlc_scanner_index"),
        ("command", f"{document['command']} --repeat 2"),
        ("todo_branch", "codex/wrong-todo3"),
        ("baseline_sha", TASK3_BASE),
    ):
        mutant = dict(document)
        mutant[key] = value
        mutations.append(mutant)
    for mutant in mutations:
        try:
            _validate_task3_red_document(mutant, scenario, identity)
        except QaError:
            continue
        raise QaError("task-3 RED receipt mutation unexpectedly accepted")
    boulder_mutant: dict[str, JsonValue] = {
        "schema_version": 2,
        "active_work_id": WORK_ID,
        "works": {},
    }
    _expect_rejection(
        lambda: _validate_task3_boulder(boulder_mutant, context.main_root),
        "task-3-boulder-mutation",
    )
    with tempfile.TemporaryDirectory(prefix="mlc-task3-symlink-") as directory:
        os.symlink(
            str(context.evidence / "task-3-red.json"),
            Path(directory) / "receipt.json",
        )
        _expect_rejection(
            lambda: _read_relative_file(Path(directory), "receipt.json"),
            "task-3-symlink-receipt",
        )
    record = (
        "MLC_QA_RECEIPT_NEGATIVE task=3 "
        "cases=symlink,boulder,wrong_branch,baseline_mismatch,cross_fixture,cross_command "
        f"status=PASS receipt_sha256={receipt_hash}"
    )
    if emit:
        print(record)
    return {
        "command": "tools/mem_leak_checker_qa.sh receipt-negative --task 3",
        "exit": 0,
        "records": [record],
        "artifacts": {"task-3-red.json": receipt_hash},
    }


def _post_task3_deferred(
    root: Path,
    evidence: Path,
    red_hash: str,
    fixtures: tuple[str, ...],
    selector: str,
    receiving: str,
    tree: str,
    baseline_patch: str,
) -> dict[str, JsonValue]:
    host_records = _run_task3_host(root, fixtures)
    fixture_hash = hashlib.sha256(",".join(fixtures).encode()).hexdigest()[:16]
    name = f"qemu-deferred-integrated-{selector}-{receiving}-{fixture_hash}.json"
    receipt_hash = _publish_task3_receipt(
        root,
        evidence,
        name,
        {
            "schema": 1,
            "task": 3,
            "receiving_sha": receiving,
            "receiving_tree": tree,
            "baseline_to_receiving_patch_sha256": baseline_patch,
            "red_receipt_sha256": red_hash,
            "fixtures": list(fixtures),
            "selector": selector,
            "status": "deferred_unexecuted_baseline_link_failure",
            "executed": False,
            "exit": None,
        },
    )
    record = (
        f"MLC_QA_QEMU task=3 fixtures={','.join(fixtures)} "
        "status=deferred_unexecuted_baseline_link_failure red_linkage=verified "
        f"receipt_sha256={receipt_hash}"
    )
    return {
        "command": (
            f"tools/mem_leak_checker_qa.sh qemu --task 3 --{selector} "
            f"{','.join(fixtures)}"
        ),
        "exit": 0,
        "records": [*host_records, record],
        "artifacts": {name: receipt_hash},
    }


def _task3_scenario_entries(
    scenario: dict[str, JsonValue],
) -> tuple[dict[str, JsonValue], ...]:
    entries: list[dict[str, JsonValue]] = []
    for section in ("happy", "failure"):
        values = scenario.get(section)
        if not isinstance(values, list):
            raise QaError(f"Task3 {section} scenario list required")
        for value in values:
            if not isinstance(value, dict):
                raise QaError(f"Task3 {section} scenario object required")
            entries.append(value)
    if len(entries) != 4:
        raise QaError("Task3 seal requires exactly four descriptor scenarios")
    return tuple(entries)


def _task3_scenario_artifacts(
    evidence: Path, records: tuple[str, ...]
) -> dict[str, JsonValue]:
    hashes = {
        match.group(1)
        for record in records
        for match in [re.search(r"(?:^| )receipt_sha256=([0-9a-f]{64})(?: |$)", record)]
        if match is not None
    }
    artifacts: dict[str, JsonValue] = {}
    for path in sorted(evidence.iterdir()):
        if path.is_symlink() or not path.is_file():
            continue
        digest = sha256_path(path)
        if digest in hashes:
            artifacts[path.name] = digest
    if len(artifacts) != len(hashes):
        raise QaError("Task3 scenario receipt artifact lookup mismatch")
    return artifacts


def _run_task3_descriptor_scenario(
    root: Path, evidence: Path, descriptor: dict[str, JsonValue]
) -> dict[str, JsonValue]:
    command = descriptor.get("command")
    if not isinstance(command, str):
        raise QaError("Task3 scenario command missing")
    command_argv = shlex.split(command)
    if not command_argv or "seal-task" in command_argv:
        raise QaError("Task3 seal scenario recursion rejected")
    completed = subprocess.run(
        command_argv,
        cwd=root,
        text=True,
        capture_output=True,
        check=False,
    )
    records = tuple(line for line in completed.stdout.splitlines() if line)
    expected_exit = descriptor.get("expected_exit")
    if isinstance(expected_exit, int):
        if completed.returncode != expected_exit:
            raise QaError(
                f"Task3 scenario exit mismatch: expected {expected_exit}, "
                f"observed {completed.returncode}: {command}"
            )
    elif expected_exit is None:
        if completed.returncode != 0:
            raise QaError(
                f"Task3 deferred scenario failed with {completed.returncode}: {command}"
            )
    else:
        raise QaError("Task3 scenario expected_exit type mismatch")
    expected_records = descriptor.get("expected_records")
    if expected_records is not None:
        if not isinstance(expected_records, list) or records != tuple(expected_records):
            raise QaError(f"Task3 scenario records mismatch: {command}")
    expected_prefix = descriptor.get("expected_record_prefix")
    if expected_prefix is not None:
        if not isinstance(expected_prefix, str):
            raise QaError("Task3 scenario record prefix type mismatch")
        matching = tuple(record for record in records if record.startswith(expected_prefix))
        if len(matching) != 1:
            raise QaError(f"Task3 scenario record prefix mismatch: {command}")
        required_fields = descriptor.get("required_fields")
        if not isinstance(required_fields, list) or not all(
            isinstance(field, str) and field in matching[0] for field in required_fields
        ):
            raise QaError(f"Task3 scenario required field mismatch: {command}")
    return {
        "command": command,
        "command_argv": command_argv,
        "exit": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "records": list(records),
        "artifacts": _task3_scenario_artifacts(evidence, records),
    }


def task_3_seal(args: argparse.Namespace) -> None:
    context = _load_task3_context()
    root = context.root
    if context.mode == "development":
        _wait_for_index_lock(context.main_root)
        _, red_hash = _read_task3_red(context)
        if _git(root, "rev-parse", args.source) != context.todo_head:
            raise QaError("Todo3 development seal source drift")
        scenario = _task3_scenario(root)
        results = [
            _run_task3_descriptor_scenario(root, context.evidence, descriptor)
            for descriptor in _task3_scenario_entries(scenario)
        ]
        identity = _task3_identity(context)
        patch_hash = identity["full_patch_sha256"]
        if not isinstance(patch_hash, str):
            raise QaError("Todo3 seal patch identity type mismatch")
        name = f"task-3-development-seal-{context.todo_head}-{patch_hash}.json"
        receipt_hash = _publish_task3_receipt(
            context.main_root,
            context.evidence,
            name,
            {
                "schema": 1,
                "task": 3,
                "status": "deferred_post_integration_required",
                "post_integration": False,
                "identity": identity,
                "red_receipt_sha256": red_hash,
                "scenarios": results,
            },
        )
        print(
            "MLC_QA_SEAL task=3 status=deferred_post_integration_required "
            f"receipt_sha256={receipt_hash}"
        )
        return
    if _git(root, "rev-parse", args.source) != _git(root, "rev-parse", "HEAD"):
        raise QaError("Todo3 post-integration branch/source mismatch")
    raw_status = subprocess.run(
        ["git", "status", "--porcelain=v1", "-z", "--untracked-files=all"],
        cwd=root,
        capture_output=True,
        check=True,
    ).stdout
    for entry in raw_status.split(b"\0"):
        if entry and not entry[3:].decode("utf-8", "strict").startswith(".omo/"):
            raise QaError("Todo3 post-integration seal rejects non-.omo product dirt")
    scenario = _task3_scenario(root)
    red_payload = _read_relative_file(root, f"{TASK3_EVIDENCE_RELATIVE}/task-3-red.json")
    red_value: JsonValue = json.loads(red_payload)
    if not isinstance(red_value, dict):
        raise QaError("Todo3 post-integration RED receipt object required")
    _validate_task3_red_document(red_value, scenario)
    fixture_files = red_value.get("fixture_files")
    if not isinstance(fixture_files, list):
        raise QaError("Todo3 post-integration fixture identities missing")
    for entry in fixture_files:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            raise QaError("Todo3 post-integration fixture entry invalid")
        relative = entry["path"]
        if entry.get("sha256") != hashlib.sha256(_read_relative_file(root, relative)).hexdigest():
            raise QaError(f"Todo3 post-integration fixture drift: {relative}")
    receiving = _git(root, "rev-parse", "HEAD")
    receiving_tree = _git(root, "write-tree")
    baseline_patch = hashlib.sha256(
        subprocess.run(
            ["git", "diff", f"{BASELINE}..{receiving}", "--binary"],
            cwd=root,
            capture_output=True,
            check=True,
        ).stdout
    ).hexdigest()
    red_hash = hashlib.sha256(red_payload).hexdigest()
    evidence = root / TASK3_EVIDENCE_RELATIVE
    results = [
        _run_task3_descriptor_scenario(root, evidence, descriptor)
        for descriptor in _task3_scenario_entries(scenario)
    ]
    name = f"task-3-post-integration-{receiving}.json"
    receipt_hash = _publish_task3_receipt(
        root,
        evidence,
        name,
        {
            "schema": 1,
            "task": 3,
            "status": "deferred_static_scenarios_sealed",
            "post_integration": True,
            "receiving_sha": receiving,
            "receiving_tree": receiving_tree,
            "baseline_to_receiving_patch_sha256": baseline_patch,
            "red_receipt_sha256": red_hash,
            "owned_fixture_files": fixture_files,
            "semantic_registration": {
                "todo2_preserved": True,
                "task3_qa_routes": True,
                "make_sources_once": True,
            },
            "scenario_sha256": hashlib.sha256(
                _read_relative_file(root, "tools/mem_leak_checker_scenarios/task-3.json")
            ).hexdigest(),
            "scenarios": results,
        },
    )
    print(
        "MLC_QA_SEAL task=3 status=deferred_static_scenarios_sealed "
        f"receipt_sha256={receipt_hash}"
    )


def task_3_toolchain_evidence() -> None:
    context = _load_task3_context()
    _wait_for_index_lock(context.main_root)
    _, red_hash = _read_task3_red(context)
    identity = _task3_identity(context)
    patch_hash = identity["full_patch_sha256"]
    if not isinstance(patch_hash, str):
        raise QaError("Task3 evidence patch identity type mismatch")
    compiler = shutil.which("cc")
    docker = shutil.which("docker")
    if compiler is None or docker is None:
        raise QaError("required host compiler or Docker is unavailable")
    compiler_identity = subprocess.run(
        [compiler, "--version"], text=True, capture_output=True, check=True
    ).stdout.splitlines()[0]
    sources = (
        "os/kernel/debug/mem_leak_checker_core.c",
        "os/kernel/debug/mem_leak_checker_core.h",
        "os/kernel/debug/mem_leak_checker_core_internal.h",
        "os/kernel/debug/mem_leak_checker_index.c",
        "os/kernel/debug/tests/test_mem_leak_checker_core.c",
    )
    source_hashes: dict[str, JsonValue] = {
        relative: hashlib.sha256(_read_relative_file(context.root, relative)).hexdigest()
        for relative in sources
    }
    with tempfile.TemporaryDirectory(prefix="tizenrt-mlc-task3-evidence-") as directory:
        temporary = Path(directory)
        sanitizer_binary = temporary / "test_mem_leak_checker_core_sanitized"
        sanitizer_command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            f"-I{context.root / 'os/kernel/debug'}",
            str(context.root / sources[4]),
            str(context.root / sources[0]),
            str(context.root / sources[3]),
            "-o",
            str(sanitizer_binary),
        ]
        subprocess.run(sanitizer_command, cwd=context.root, check=True)
        sanitizer_binary_hash = sha256_path(sanitizer_binary)
        sanitizer_runs = (
            "mlc_scanner_index",
            "mlc_zero_exact_precedence",
            "mlc_scanner_invalid_ranges",
        )
        sanitizer_environment = {
            **os.environ,
            "ASAN_OPTIONS": "detect_leaks=0",
            "UBSAN_OPTIONS": "halt_on_error=1",
        }
        for _ in range(100):
            for fixture in sanitizer_runs:
                subprocess.run(
                    [str(sanitizer_binary), fixture],
                    cwd=context.root,
                    env=sanitizer_environment,
                    stdout=subprocess.DEVNULL,
                    check=True,
                )
        sanitizer_name = f"task-3-sanitizer-{patch_hash}.json"
        _publish_task3_receipt(
            context.main_root,
            context.evidence,
            sanitizer_name,
            {
                "schema": 1,
                "task": 3,
                "kind": "asan-ubsan",
                "identity": identity,
                "red_receipt_sha256": red_hash,
                "source_hashes": source_hashes,
                "compiler": compiler_identity,
                "compile_command": [
                    _stable_task3_text(item, context, temporary)
                    for item in sanitizer_command
                ],
                "run_command": ["$TMP/test_mem_leak_checker_core_sanitized", "<fixture>"],
                "environment": {
                    "ASAN_OPTIONS": "detect_leaks=0",
                    "UBSAN_OPTIONS": "halt_on_error=1",
                },
                "fixtures": list(sanitizer_runs),
                "repetitions": 100,
                "binary_sha256": sanitizer_binary_hash,
                "status": "PASS",
                "leak_sanitizer": "unsupported_platform",
            },
        )
        sanitizer_receipt = context.evidence / sanitizer_name

        mutation_root = temporary / "mutation"
        mutation_test_dir = mutation_root / "tests"
        mutation_test_dir.mkdir(parents=True)
        for relative in sources[:4]:
            shutil.copy2(context.root / relative, mutation_root / Path(relative).name)
        shutil.copy2(context.root / sources[4], mutation_test_dir / Path(sources[4]).name)
        mutation_source = mutation_root / "mem_leak_checker_core.c"
        original = mutation_source.read_text(encoding="utf-8")
        mutated = original.replace(
            "mlc_candidate_lookup_validated(index, value, &found, &lookup)",
            "mlc_candidate_lookup(index, value, &found, &lookup)",
            1,
        )
        if mutated == original:
            raise QaError("Task3 validation mutation target missing")
        mutation_source.write_text(mutated, encoding="utf-8")
        mutation_binary = temporary / "test_mem_leak_checker_core_mutated"
        mutation_compile = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            f"-I{mutation_root}",
            str(mutation_test_dir / Path(sources[4]).name),
            str(mutation_source),
            str(mutation_root / "mem_leak_checker_index.c"),
            "-o",
            str(mutation_binary),
        ]
        subprocess.run(mutation_compile, check=True)
        mutation_run = subprocess.run(
            [str(mutation_binary), "mlc_scanner_index"],
            text=True,
            capture_output=True,
            check=False,
        )
        if mutation_run.returncode == 0:
            raise QaError("Task3 validation mutation unexpectedly passed")
        mutation_name = f"task-3-mutation-{patch_hash}.json"
        _publish_task3_receipt(
            context.main_root,
            context.evidence,
            mutation_name,
            {
                "schema": 1,
                "task": 3,
                "kind": "validation-mutation",
                "identity": identity,
                "red_receipt_sha256": red_hash,
                "source_hashes": source_hashes,
                "compiler": compiler_identity,
                "mutation": "private validated lookup replaced by public full-validation lookup",
                "mutated_source_sha256": sha256_path(mutation_source),
                "compile_command": [
                    _stable_task3_text(item, context, temporary)
                    for item in mutation_compile
                ],
                "run_command": ["$TMP/test_mem_leak_checker_core_mutated", "mlc_scanner_index"],
                "exit": mutation_run.returncode,
                "stderr": _stable_task3_text(
                    mutation_run.stderr.strip(), context, temporary
                ),
                "status": "PASS_MUTATION_REJECTED",
            },
        )
        mutation_receipt = context.evidence / mutation_name

        gcc_root = temporary / "gcc6"
        gcc_root.mkdir()
        for relative in sources[:4]:
            shutil.copy2(context.root / relative, gcc_root / Path(relative).name)
        container_command = (
            "arm-none-eabi-gcc --version | sed -n '1p'; "
            "arm-none-eabi-gcc -std=gnu99 -mcpu=cortex-m4 -mthumb -ffreestanding "
            "-fno-builtin -Wall -Wextra -Werror -I. -c mem_leak_checker_core.c "
            "-o mem_leak_checker_core.o; "
            "arm-none-eabi-gcc -std=gnu99 -mcpu=cortex-m4 -mthumb -ffreestanding "
            "-fno-builtin -Wall -Wextra -Werror -I. -c mem_leak_checker_index.c "
            "-o mem_leak_checker_index.o"
        )
        gcc_command = [
            docker,
            "run",
            "--rm",
            "-v",
            f"{gcc_root}:/work",
            "-w",
            "/work",
            "tizenrt/tizenrt:1.5.8",
            "sh",
            "-c",
            container_command,
        ]
        gcc_run = subprocess.run(gcc_command, text=True, capture_output=True, check=True)
        gcc_identity = next(
            line for line in gcc_run.stdout.splitlines() if "arm-none-eabi-gcc" in line
        )
        object_hashes: dict[str, JsonValue] = {
            name: sha256_path(gcc_root / name)
            for name in ("mem_leak_checker_core.o", "mem_leak_checker_index.o")
        }
        gcc_name = f"task-3-arm-gcc6-{patch_hash}.json"
        _publish_task3_receipt(
            context.main_root,
            context.evidence,
            gcc_name,
            {
                "schema": 1,
                "task": 3,
                "kind": "arm-gcc6",
                "identity": identity,
                "red_receipt_sha256": red_hash,
                "source_hashes": source_hashes,
                "command": [
                    _stable_task3_text(item, context, temporary) for item in gcc_command
                ],
                "compiler": gcc_identity,
                "object_sha256": object_hashes,
                "status": "PASS",
                "rerun_limitation": None,
            },
        )
        gcc_receipt = context.evidence / gcc_name
    print(
        "MLC_QA_TASK3_EVIDENCE status=PASS "
        f"sanitizer_sha256={sha256_path(sanitizer_receipt)} "
        f"mutation_sha256={sha256_path(mutation_receipt)} "
        f"arm_gcc6_sha256={sha256_path(gcc_receipt)}"
    )


def _fixture_digest(root: Path) -> tuple[list[dict[str, JsonValue]], str]:
    paths = sorted(KNOWN_TODO_PATHS)
    entries = [
        {"path": path, "sha256": sha256_path(root / path), "size": (root / path).stat().st_size}
        for path in paths
    ]
    digest = hashlib.sha256(canonical_bytes(entries)).hexdigest()
    return entries, digest


def scenario_static(context: RunnerContext, output: Path) -> None:
    try:
        output.relative_to(context.evidence)
    except ValueError as error:
        raise QaError("scenario receipt must remain in canonical evidence") from error
    scenario_path = context.root / "tools/mem_leak_checker_scenarios/task-1.json"
    scenario = _load_json_object(scenario_path)
    _validate_scenario(scenario)
    self_test_path = context.evidence / "task-1/preflight-self-test.json"
    self_test = _load_json_object(self_test_path)
    case_records = self_test.get("cases")
    if (
        self_test.get("exit") != 0
        or not isinstance(case_records, list)
        or [record.get("case") for record in case_records if isinstance(record, dict)]
        != list(SELF_TEST_CASES)
        or any(
            not isinstance(record, dict) or record.get("status") != "PASS" for record in case_records
        )
    ):
        raise QaError("canonical self-test receipt does not satisfy the failure scenario")
    fixtures, fixture_digest = _fixture_digest(context.root)
    patch = subprocess.run(
        ["git", "diff", "--binary", "--", *sorted(KNOWN_TODO_PATHS)],
        cwd=context.root,
        capture_output=True,
        check=True,
    ).stdout
    untracked_entries = [
        entry
        for entry in fixtures
        if subprocess.run(
            ["git", "ls-files", "--error-unmatch", entry["path"]],
            cwd=context.root,
            capture_output=True,
            check=False,
        ).returncode
        != 0
    ]
    patch_material = patch + canonical_bytes(untracked_entries)
    publish_json(
        output,
        {
            "schema": 1,
            "task": 1,
            "baseline_sha": context.baseline_sha,
            "receiving_sha": context.head_sha,
            "tree": _git(context.root, "write-tree"),
            "patch_sha256": hashlib.sha256(patch_material).hexdigest(),
            "scenario": {"path": str(scenario_path), "sha256": sha256_path(scenario_path)},
            "fixtures": fixtures,
            "fixture_digest": fixture_digest,
            "scenarios": [
                {
                    "kind": "happy",
                    "status": "deferred_unexecuted_baseline_link_failure",
                    "executed": False,
                    "exit": None,
                    "expected_records": scenario["happy"][0]["expected_records"],
                },
                {
                    "kind": "failure",
                    "status": "validated_by_self_test_receipt",
                    "expected_exit": 0,
                    "receipt": {"path": str(self_test_path), "sha256": sha256_path(self_test_path)},
                },
            ],
            "exit": 0,
        },
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    qemu = subparsers.add_parser("qemu-session")
    qemu.add_argument("--task", type=int, required=True)
    qemu.add_argument("--fixtures", required=True)
    qemu.add_argument("--binary", required=True)
    qemu.add_argument("--timeout", type=float, default=180.0)
    self_test = subparsers.add_parser("self-test")
    self_test.add_argument("--task", type=int, required=True)
    self_test.add_argument("--cases", required=True)
    self_test.add_argument("--output-json", type=Path, required=True)
    context = subparsers.add_parser("context")
    context.add_argument("--print-evidence-dir", action="store_true")
    context.add_argument("--print-baseline-sha", action="store_true")
    validate_receipt = subparsers.add_parser("validate-receipt")
    validate_receipt.add_argument("--path", type=Path, required=True)
    validate_scenario = subparsers.add_parser("validate-scenario")
    validate_scenario.add_argument("--path", type=Path, required=True)
    validate_boulder = subparsers.add_parser("validate-boulder")
    validate_boulder.add_argument("--path", type=Path, required=True)
    validate_boulder.add_argument("--root", type=Path, required=True)
    scenario = subparsers.add_parser("scenario-static")
    scenario.add_argument("--output-json", type=Path, required=True)
    deferred = subparsers.add_parser("task-3-deferred")
    deferred.add_argument("--fixtures", required=True)
    deferred.add_argument("--selector", choices=("fixture", "fixtures"), required=True)
    task3_red = subparsers.add_parser("task-3-red")
    task3_red.add_argument("--fixtures", required=True)
    subparsers.add_parser("task-3-receipt-negative")
    task3_seal = subparsers.add_parser("task-3-seal")
    task3_seal.add_argument("--source", required=True)
    subparsers.add_parser("task-3-toolchain-evidence")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        match args.command:  # noqa: MATCH_OK -- all remaining commands require validated context.
            case "validate-receipt":
                _validate_red(_load_json_object(args.path.resolve()))
                return 0
            case "validate-scenario":
                _validate_scenario(_load_json_object(args.path.resolve()))
                return 0
            case "validate-boulder":
                _parse_boulder(args.path.resolve(), args.root.resolve())
                return 0
            case "task-3-deferred":
                task_3_deferred(args)
                return 0
            case "task-3-red":
                return task_3_red(args)
            case "task-3-receipt-negative":
                task_3_receipt_negative()
                return 0
            case "task-3-seal":
                task_3_seal(args)
                return 0
            case "task-3-toolchain-evidence":
                task_3_toolchain_evidence()
                return 0
            case _:
                context = load_context()
        match args.command:
            case "qemu-session":
                qemu_command(args, context)
            case "self-test":
                if args.task != 1:
                    raise QaError("Todo 1 runner accepts only task 1")
                run_self_tests(tuple(args.cases.split(",")), args.output_json.resolve(), context)
            case "context":
                if args.print_evidence_dir == args.print_baseline_sha:
                    raise QaError("context requires exactly one print selector")
                print(context.evidence if args.print_evidence_dir else context.baseline_sha)
            case "scenario-static":
                scenario_static(context, args.output_json.resolve())
            case unexpected:
                assert_never(unexpected)
    except (OSError, QaError, subprocess.SubprocessError, ValueError) as error:
        print(f"mem-leak-checker QA failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
