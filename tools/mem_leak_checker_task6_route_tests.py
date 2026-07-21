from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import signal
import subprocess

from mem_leak_checker_task6_validate import ContractError, ExpectedContext, derive_context
from mem_leak_checker_task6_validate_test_support import make_fixture, run_git


def reject_context(expected: ExpectedContext) -> None:
    try:
        derive_context(expected)
    except ContractError:
        return
    raise AssertionError("context mutation accepted")


def test_codegraph(parent: Path) -> int:
    valid_parent = parent / "codegraph-valid"
    valid_parent.mkdir()
    root, expected = make_fixture(valid_parent)
    external = root / ".codegraph"
    external.mkdir()
    (external / "index.db").write_text("ignored\n")
    assert derive_context(expected).external_state_exclusions == (".codegraph:unauthenticated_external_state",)
    count = 1
    for name, setup in (
        ("root-symlink", lambda root: (root / ".codegraph").symlink_to(root / "outside", target_is_directory=True)),
        ("entry-symlink", lambda root: ((root / ".codegraph").mkdir(), (root / ".codegraph/link").symlink_to(root / "outside"))),
        ("nonignored", lambda root: ((root / ".codegraph").mkdir(), (root / ".codegraph/file.txt").write_text("x"), (root / ".gitignore").write_text(".omo/\nignored.tmp\n"), run_git(root, "add", ".gitignore"), run_git(root, "commit", "-q", "-m", "ignore-change"))),
    ):
        case = parent / name
        case.mkdir()
        root, expected = make_fixture(case)
        (root / "outside").mkdir()
        setup(root)
        reject_context(expected)
        count += 1
    tracked_parent = parent / "tracked"
    tracked_parent.mkdir()
    root, expected = make_fixture(tracked_parent)
    (root / ".codegraph").mkdir()
    (root / ".codegraph/tracked.db").write_text("tracked\n")
    run_git(root, "add", "-f", ".codegraph/tracked.db")
    run_git(root, "commit", "-q", "-m", "tracked codegraph")
    reject_context(expected)
    count += 1
    special_parent = parent / "special"
    special_parent.mkdir()
    root, expected = make_fixture(special_parent)
    (root / ".codegraph").mkdir()
    os.mkfifo(root / ".codegraph/fifo")
    reject_context(expected)
    count += 1
    return count


def test_route_preflight(parent: Path) -> int:
    root, expected = make_fixture(parent)
    source_tools = Path(__file__).resolve().parent
    names = (
        "mem_leak_checker_qa.sh",
        "mem_leak_checker_task6_qa.sh", "mem_leak_checker_task6_validate.py",
        "mem_leak_checker_task6_context.py", "mem_leak_checker_task6_schema.py",
        "mem_leak_checker_task6_types.py", "mem_leak_checker_task6_manifest.py",
        "mem_leak_checker_task6_git_trust.py", "mem_leak_checker_task6_receipt.py",
        "mem_leak_checker_task6_publish.py", "mem_leak_checker_task6_seal.sh",
        "mem_leak_checker_task6_files.py", "mem_leak_checker_task6_output.py",
        "mem_leak_checker_task6_host_qa.sh", "mem_leak_checker_task6_runner.py",
        "test_mem_leak_checker_task6_validate.py",
    )
    for name in names:
        shutil.copy2(source_tools / name, root / "tools" / name)
    run_git(root, "add", *(f"tools/{name}" for name in names))
    run_git(root, "commit", "-q", "-m", "route fixture")
    fixture = {field: str(getattr(expected, field)) for field in expected.__dataclass_fields__}
    hostile = parent / "hostile"
    hostile.mkdir()
    marker = parent / "hostile-python-ran"
    bash_marker = parent / "hostile-bash-ran"
    bash_env_marker = parent / "hostile-bash-env-ran"
    (hostile / "python3").write_text(f"#!/bin/sh\ntouch '{marker}'\nexit 0\n")
    (hostile / "python3").chmod(0o755)
    (hostile / "bash").write_text(f"#!/bin/sh\ntouch '{bash_marker}'\nexit 91\n")
    (hostile / "bash").chmod(0o755)
    bash_environment = parent / "bash-environment.sh"
    bash_environment.write_text(f"touch '{bash_env_marker}'\n")
    environment = {
        **os.environ, "MLC_TASK6_CONTEXT_FIXTURE": json.dumps(fixture),
        "PATH": f"{hostile}:/bin", "GIT_DIR": "/tmp/hostile-git-dir",
        "GIT_WORK_TREE": "/tmp/hostile-git-tree", "PYTHONPATH": str(hostile),
        "BASH_ENV": str(bash_environment),
    }
    script = root / "tools/mem_leak_checker_task6_qa.sh"
    dispatcher = root / "tools/mem_leak_checker_qa.sh"
    dispatched = subprocess.run([str(dispatcher), "red", "--task", "6", "--config", "qemu/tc_1m", "--fixture", "mlc_domain_pin_production_path"], cwd=root, env=environment, capture_output=True, text=True)
    if dispatched.returncode != 86 or "MLC_TASK6_RED" not in dispatched.stdout:
        raise AssertionError(f"trusted dispatcher RED route failed: {dispatched.returncode=} {dispatched.stdout=} {dispatched.stderr=}")
    if bash_marker.exists() or bash_env_marker.exists():
        raise AssertionError("hostile Bash startup executed before preflight")
    valid = subprocess.run([str(script), "red", "--config", "qemu/tc_1m", "--fixture", "mlc_domain_pin_production_path"], cwd=root, env=environment, capture_output=True, text=True)
    if valid.returncode != 86 or "MLC_TASK6_RED" not in valid.stdout:
        raise AssertionError(f"valid fixture RED route failed: {valid.returncode=} {valid.stdout=} {valid.stderr=}")
    if marker.exists():
        raise AssertionError("hostile PATH python executed")
    count = 2
    for signum, expected_exit in (
        (signal.SIGHUP, 129), (signal.SIGINT, 130), (signal.SIGTERM, 143),
    ):
        interrupted = subprocess.Popen(
            [str(script), "signal-probe"], cwd=root, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            start_new_session=True,
        )
        assert interrupted.stdout is not None
        ready = interrupted.stdout.readline().strip()
        if not ready.startswith("MLC_TASK6_SIGNAL_READY temp="):
            raise AssertionError(f"signal probe did not become ready: {ready!r}")
        temporary = Path(ready.split("=", 1)[1])
        os.killpg(interrupted.pid, signum)
        if interrupted.wait(timeout=5) != expected_exit or temporary.exists():
            raise AssertionError(f"signal cleanup failed: {signum=}")
        count += 1
    alternate_environment = {**environment, "MLC_TASK6_EVIDENCE_DIR": "/tmp/alternate-task6-evidence"}
    alternate = subprocess.run([str(script), "red", "--config", "qemu/tc_1m", "--fixture", "mlc_domain_pin_production_path"], cwd=root, env=alternate_environment, capture_output=True, text=True)
    if alternate.returncode == 0 or "MLC_TASK6_" in alternate.stdout:
        raise AssertionError("alternate evidence path reached RED route")
    count += 1
    qa = root / "tools/mem_leak_checker_task6_qa.sh"
    original = qa.read_bytes()
    run_git(root, "update-index", "--assume-unchanged", "tools/mem_leak_checker_task6_qa.sh")
    qa.write_bytes(original + b"\n# hidden mutation\n")
    hidden = subprocess.run([str(script), "red", "--config", "qemu/tc_1m", "--fixture", "mlc_domain_pin_production_path"], cwd=root, env=environment, capture_output=True, text=True)
    if hidden.returncode == 86 or "MLC_TASK6_" in hidden.stdout:
        raise AssertionError("hidden modified QA input reached RED route")
    qa.write_bytes(original)
    run_git(root, "update-index", "--no-assume-unchanged", "tools/mem_leak_checker_task6_qa.sh")
    count += 1
    run_git(root, "update-index", "--skip-worktree", "tools/mem_leak_checker_scenarios/task-6.json")
    skipped = subprocess.run([str(script), "red", "--config", "qemu/tc_1m", "--fixture", "mlc_domain_pin_production_path"], cwd=root, env=environment, capture_output=True, text=True)
    if skipped.returncode == 86 or "MLC_TASK6_" in skipped.stdout:
        raise AssertionError("skip-worktree input reached RED route")
    run_git(root, "update-index", "--no-skip-worktree", "tools/mem_leak_checker_scenarios/task-6.json")
    count += 1
    head = run_git(root, "rev-parse", "HEAD")
    run_git(root, "replace", head, expected.baseline_sha)
    replaced = subprocess.run([str(script), "red", "--config", "qemu/tc_1m", "--fixture", "mlc_domain_pin_production_path"], cwd=root, env=environment, capture_output=True, text=True)
    if replaced.returncode == 86 or "MLC_TASK6_" in replaced.stdout:
        raise AssertionError("replace-ref context reached RED route")
    run_git(root, "replace", "-d", head)
    count += 1
    for key, value in (("core.fsmonitor", "true"), ("core.hooksPath", "/tmp/hostile-hooks")):
        run_git(root, "config", "--local", key, value)
        configured = subprocess.run([str(script), "red", "--config", "qemu/tc_1m", "--fixture", "mlc_domain_pin_production_path"], cwd=root, env=environment, capture_output=True, text=True)
        if configured.returncode == 86 or "MLC_TASK6_" in configured.stdout:
            raise AssertionError(f"unsafe {key} reached RED route")
        run_git(root, "config", "--local", "--unset", key)
        count += 1
    run_git(root, "config", "extensions.worktreeConfig", "true")
    run_git(root, "config", "--worktree", "core.fsmonitor", "true")
    worktree_configured = subprocess.run([str(script), "red", "--config", "qemu/tc_1m", "--fixture", "mlc_domain_pin_production_path"], cwd=root, env=environment, capture_output=True, text=True)
    if worktree_configured.returncode == 86 or "MLC_TASK6_" in worktree_configured.stdout:
        raise AssertionError("worktree-scoped fsmonitor reached RED route")
    run_git(root, "config", "--worktree", "--unset", "core.fsmonitor")
    run_git(root, "config", "--unset", "extensions.worktreeConfig")
    count += 1
    (root / ".omo/boulder.json").write_text("{}")
    for mode, arguments in (
        ("red", ("--config", "qemu/tc_1m", "--fixture", "mlc_domain_pin_production_path")),
        ("qemu", ("--fixtures", "mlc_domain_pin_production_path,mlc_try_heap_fresh_accounting,mlc_heap_release_nested_critical")),
        ("fatal", ("--fixtures", "mlc_heap_release_ownership_fatal,mlc_domain_unpin_fatal")),
    ):
        result = subprocess.run([str(script), mode, *arguments], cwd=root, env=environment, capture_output=True, text=True)
        if result.returncode == 0 or "MLC_TASK6_" in result.stdout:
            raise AssertionError(f"invalid context reached {mode} route")
        count += 1
    return count
