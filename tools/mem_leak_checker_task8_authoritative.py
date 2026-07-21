from __future__ import annotations

import fcntl
import hashlib
import json
import os
import shlex
import stat
import subprocess
import sys
import tempfile
import types
from pathlib import Path
from typing import Final

GIT: Final = "/usr/bin/git"
BASH: Final = "/opt/homebrew/bin/bash"
THREAT_MODEL: Final = {
    "active_same_uid_race": "excluded",
    "checked_inputs": "postvalidated",
    "trusted_initial_root": "tools/mem_leak_checker_task8_qa.sh_body_before_python_bootstrap_cannot_self_authenticate",
    "shell_startup_before_script_body": "excluded",
    "scope": "cooperative_local_qa",
    "trusted_entrypoint_bootstrap": "required_before_validation",
}
BOOTSTRAP_PATHS: Final = (
    "tools/mem_leak_checker_qa.sh",
    "tools/mem_leak_checker_qa_core.py",
    "tools/mem_leak_checker_task4_evidence.py",
    "tools/mem_leak_checker_task4_scenarios.py",
    "tools/mem_leak_checker_task4_authoritative.py",
    "tools/mem_leak_checker_task5_scenarios.sh",
    "tools/mem_leak_checker_task8_authoritative.py",
    "tools/mem_leak_checker_task8_qa.sh",
)


class Task8BootstrapError(RuntimeError):
    pass


def clean_environment() -> dict[str, str]:
    return {
        "GIT_CONFIG_GLOBAL": "/dev/null",
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_NO_REPLACE_OBJECTS": "1",
        "LC_ALL": "C",
        "PATH": "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
        "TMPDIR": "/tmp",
    }


def git_run(
    root: Path,
    *arguments: str,
    text: bool = False,
) -> subprocess.CompletedProcess[bytes] | subprocess.CompletedProcess[str]:
    return subprocess.run(
        [GIT, "-C", str(root), *arguments],
        check=True,
        capture_output=True,
        env=clean_environment(),
        text=text,
    )


def reject_replace_refs(root: Path) -> None:
    if git_run(root, "for-each-ref", "--format=%(refname)", "refs/replace", text=True).stdout:
        raise Task8BootstrapError("task-8 replace refs rejected")


def bootstrap_verify() -> None:
    if len(sys.argv) != 4:
        return
    root = Path(sys.argv[1]).resolve(strict=True)
    reject_replace_refs(root)
    index_flags = git_run(root, "ls-files", "-v", "--", *BOOTSTRAP_PATHS).stdout.splitlines()
    if len(index_flags) != len(BOOTSTRAP_PATHS) or any(
        record[:1] == b"S" or record[:1].islower() for record in index_flags
    ):
        raise Task8BootstrapError("task-8 bootstrap index flags rejected")
    for relative in BOOTSTRAP_PATHS:
        path = root / relative
        metadata = path.lstat()
        if not stat.S_ISREG(metadata.st_mode):
            raise Task8BootstrapError(f"task-8 bootstrap path rejected: {relative}")
        committed = git_run(root, "show", f"HEAD:{relative}").stdout
        if path.read_bytes() != committed:
            raise Task8BootstrapError(f"task-8 bootstrap content drift: {relative}")


def source_module(name: str) -> types.ModuleType:
    path = Path(__file__).resolve().parent / f"{name}.py"
    module = types.ModuleType(name)
    module.__file__ = str(path)
    module.__package__ = ""
    sys.modules[name] = module
    exec(compile(path.read_bytes(), str(path), "exec"), module.__dict__)
    return module


bootstrap_verify()

for source_name in (
    "mem_leak_checker_qa_core",
    "mem_leak_checker_task4_evidence",
    "mem_leak_checker_task4_scenarios",
    "mem_leak_checker_task4_authoritative",
):
    source_module(source_name)

from mem_leak_checker_qa_core import JsonValue, QaError
from mem_leak_checker_task4_authoritative import PUBLICATION, publish

SCENARIO: Final = "tools/mem_leak_checker_scenarios/task-8.json"
RED_PATHS: Final = (
    "tools/mem_leak_checker_scenarios/task-8.json",
    "tools/mem_leak_checker_task8_qa.sh",
    "tools/tests/mem_leak_checker_task_roots_model.c",
)
EXPECTED_SCENARIOS: Final = {
    "red": {
        "command": "tools/mem_leak_checker_qa.sh red --task 8 --config qemu/tc_1m --fixture mlc_task_roots",
        "expected_exit": 86,
        "expected_records": [
            "MLC_TASK8_RED status=expected_failure exit=86 evidence=development_only authoritative=false",
        ],
    },
    "happy": {
        "command": "tools/mem_leak_checker_qa.sh qemu --task 8 --fixtures mlc_task_roots,mlc_direct_wrapper_roots --repeat 1 --post-commit",
        "expected_exit": 0,
        "expected_records": [
            "MLC_TASK8_ROUTE kind=happy selector=fixtures fixtures=mlc_task_roots,mlc_direct_wrapper_roots repeat=1 post_commit=true",
            "MLC_TASK8_MODEL status=PASS roots=callee_saved,stack saved_context=blocked,remote_paused architectures=armv7a,armv7m",
            "MLC_TASK8_PRODUCTION_ROOTS status=PASS roots=saved_task cpsr=usr,svc,sys xpsr=thumb released=1",
            "MLC_TASK8_WRAPPERS status=PASS prctl=assembly-entry direct=assembly-capture",
            "MLC_TASK8_ASSEMBLY status=PASS armv7_m=compiled armv7_a=compiled",
            "MLC_TASK8_OBJECTS status=PASS armv7_m=3 armv7_a=3",
            "MLC_TASK8_ENTRY_CONTRACT status=PASS direct_record=72 direct_entry_sp=true prctl_entry_sp=true clang_opts=O0,O1,O2,O3,Os gnu=unverified",
            "MLC_TASK8_SAVED_STATUS status=PASS cpsr=usr,svc,sys xpsr=thumb",
            "MLC_TASK8_QEMU status=deferred_unexecuted_baseline_link_failure",
        ],
    },
    "failure": {
        "command": "tools/mem_leak_checker_qa.sh qemu --task 8 --fixture mlc_invalid_task_irq_context --repeat 1 --post-commit",
        "expected_exit": 0,
        "expected_records": [
            "MLC_TASK8_ROUTE kind=failure selector=fixture fixtures=mlc_invalid_task_irq_context repeat=1 post_commit=true",
            "MLC_TASK8_FAILURES model_status=PASS expected_incomplete=TASK_CONTEXT expected_rows=0 mutations=migration,tcb,irq,mode,sp,mask,cpsr,xpsr reuse=valid_after_rejection",
            "MLC_TASK8_PRODUCTION_FAILURE status=PASS mutations=cpsr_fiq,cpsr_irq,cpsr_reserved,xpsr_thumb,xpsr_ipsr,sp,migration incomplete=TASK_CONTEXT rows=0 released=1 reuse=valid_after_rejection",
            "MLC_TASK8_QEMU status=deferred_unexecuted_baseline_link_failure",
        ],
    },
}
CONTENT_PATHS: Final = (
    "os/arch/arm/src/amebasmart/Make.defs",
    "os/arch/arm/src/armv7-a/arm_mem_leak_capture.S",
    "os/arch/arm/src/armv7-m/arm_mem_leak_capture.S",
    "os/arch/arm/src/tiva/Make.defs",
    "os/include/tinyara/arch.h",
    "os/kernel/debug/Make.defs",
    "os/kernel/debug/mem_leak_checker.c",
    "os/kernel/debug/mem_leak_checker_candidates.h",
    "os/kernel/debug/mem_leak_checker_candidates_internal.h",
    "os/kernel/debug/mem_leak_checker_core.h",
    "os/kernel/debug/mem_leak_checker_domain.h",
    "os/kernel/debug/mem_leak_checker_lifecycle.c",
    "os/kernel/debug/mem_leak_checker_lifecycle.h",
    "os/kernel/debug/mem_leak_checker_pause.h",
    "os/kernel/debug/mem_leak_checker_pause_owner.h",
    "os/kernel/debug/mem_leak_checker_roots.c",
    "os/kernel/debug/mem_leak_checker_roots.h",
    "os/kernel/debug/mem_leak_checker_roots_test.c",
    "os/kernel/task/task_prctl.c",
    "tools/mem_leak_checker_qa.sh",
    "tools/mem_leak_checker_scenarios/task-8.json",
    "tools/mem_leak_checker_task4_authoritative.py",
    "tools/mem_leak_checker_task5_scenarios.sh",
    "tools/mem_leak_checker_task8_authoritative.py",
    "tools/mem_leak_checker_task8_qa.sh",
    "tools/mem_leak_checker_task8_seal_test.sh",
    "tools/tests/mem_leak_checker_production_roots_fixture.c",
    "tools/tests/mem_leak_checker_stubs/sched/sched.h",
    "tools/tests/mem_leak_checker_stubs/tinyara/arch.h",
    "tools/tests/mem_leak_checker_stubs/tinyara/compiler.h",
    "tools/tests/mem_leak_checker_stubs/tinyara/config.h",
    "tools/tests/mem_leak_checker_stubs/tinyara/irq.h",
    "tools/tests/mem_leak_checker_stubs/tinyara/sched.h",
    "tools/tests/mem_leak_checker_stubs/tinyara/spinlock.h",
    "tools/tests/mem_leak_checker_task_roots_model.c",
)


def git_blob(root: Path, sha: str, relative: str) -> bytes:
    return git_run(root, "show", f"{sha}:{relative}").stdout


def validate_context(root: Path, receipt: Path) -> str:
    completed = subprocess.run(
        [
            BASH,
            str(root / "tools/mem_leak_checker_task5_scenarios.sh"),
            "validate-receiving",
            "--receipt",
            str(receipt.relative_to(root)),
            "--task",
            "8",
        ],
        cwd=root,
        env=clean_environment(),
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise QaError(completed.stderr.strip() or "task-8 authenticated context rejected")
    digest = completed.stdout.strip()
    if len(digest) != 64 or any(value not in "0123456789abcdef" for value in digest):
        raise QaError("task-8 normalized plan digest malformed")
    return digest


def scenario_document(root: Path) -> dict[str, JsonValue]:
    value = json.loads((root / SCENARIO).read_bytes())
    if set(value) != {"failure", "happy", "qemu", "red", "red_exempt", "schema", "task"}:
        raise QaError("task-8 scenario schema drift")
    if (
        value["schema"] != 1 or value["task"] != 8 or value["red_exempt"] is not False
        or value["qemu"] != "deferred_unexecuted_baseline_link_failure"
    ):
        raise QaError("task-8 scenario identity drift")
    for bucket, expected_exit in (("red", 86), ("happy", 0), ("failure", 0)):
        entries = value[bucket]
        if not isinstance(entries, list) or len(entries) != 1:
            raise QaError(f"task-8 {bucket} cardinality drift")
        entry = entries[0]
        if not isinstance(entry, dict) or entry != EXPECTED_SCENARIOS[bucket] or entry.get("expected_exit") != expected_exit:
            raise QaError(f"task-8 {bucket} exit drift")
        if not isinstance(entry.get("command"), str) or not isinstance(entry.get("expected_records"), list):
            raise QaError(f"task-8 {bucket} contract malformed")
    return value


def exact_run(root: Path, entry: dict[str, JsonValue], artifact: Path) -> tuple[str, int]:
    command = entry["command"]
    records = entry["expected_records"]
    if not isinstance(command, str) or not isinstance(records, list):
        raise QaError("task-8 command contract malformed")
    environment = clean_environment()
    injection = os.environ.get("MLC_TASK8_TRANSCRIPT_INJECTION")
    git_admin_injection = os.environ.get("MLC_TASK8_GIT_ADMIN_INJECTION")
    if git_admin_injection is not None:
        if git_admin_injection not in {
            "assume-unchanged", "fsmonitor", "local-config", "replace-ref", "skip-worktree",
        }:
            raise QaError("task-8 git admin injection selector rejected")
        environment["MLC_TASK8_GIT_ADMIN_INJECTION"] = git_admin_injection
    post_use_injection = os.environ.get("MLC_TASK8_POST_USE_INJECTION")
    if post_use_injection is not None:
        if post_use_injection not in {"receiving-untracked", "alternates"}:
            raise QaError("task-8 post-use injection selector rejected")
        environment["MLC_TASK8_POST_USE_INJECTION"] = post_use_injection
        if post_use_injection == "alternates":
            alternate = os.environ.get("MLC_TASK8_ALTERNATE_INJECTION_DIR", "")
            if not alternate.startswith((
                "/private/tmp/mlc-task5-seal-test.",
                "/tmp/mlc-task5-seal-test.",
            )):
                raise QaError("task-8 alternate injection path rejected")
            environment["MLC_TASK8_ALTERNATE_INJECTION_DIR"] = alternate
    completed = subprocess.run(
        shlex.split(command),
        cwd=root,
        env={**environment, "MLC_TASK8_ARTIFACT_DIR": str(artifact)},
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    actual_stdout = completed.stdout
    actual_stderr = completed.stderr
    actual_exit = completed.returncode
    match injection:
        case None:
            pass
        case "blank":
            actual_stdout += b"\n"
        case "cross":
            actual_stdout += (
                b"MLC_TASK8_FAILURES model_status=PASS expected_incomplete=TASK_CONTEXT "
                b"expected_rows=0 mutations=migration,tcb,irq,mode,sp,mask,cpsr,xpsr "
                b"reuse=valid_after_rejection\n"
            )
        case "duplicate":
            if not records or not isinstance(records[0], str):
                raise QaError("task-8 duplicate injection target malformed")
            actual_stdout += f"{records[0]}\n".encode()
        case "exit":
            actual_exit = 87
        case "missing":
            if not records or not isinstance(records[-1], str):
                raise QaError("task-8 missing injection target malformed")
            final_record = f"{records[-1]}\n".encode()
            if not actual_stdout.endswith(final_record):
                raise QaError("task-8 missing injection target absent")
            actual_stdout = actual_stdout[: -len(final_record)]
        case "stderr":
            actual_stderr += b"MLC_TASK8_INJECTED_STDERR\n"
        case "surplus":
            actual_stdout += b"MLC_TASK8_SURPLUS status=PASS\n"
        case "unterminated":
            actual_stdout += b"MLC_TASK8_UNTERMINATED"
        case _:
            raise QaError("task-8 transcript injection selector rejected")
    expected_exit = entry["expected_exit"]
    if actual_exit != expected_exit:
        raise QaError(f"task-8 command exit drift: {command}")
    if not all(isinstance(record, str) for record in records):
        raise QaError(f"task-8 expected record contract malformed: {command}")
    expected_stdout = b"".join(f"{record}\n".encode() for record in records)
    if actual_stdout != expected_stdout or actual_stderr != b"":
        raise QaError(f"task-8 exact record drift: {command}")
    return command, actual_exit


def verify_red(root: Path, artifact: Path, entry: dict[str, JsonValue], sha: str) -> None:
    exact_run(root, entry, artifact)
    receipt = artifact / "task-8-red.json"
    value = json.loads(receipt.read_bytes())
    if set(value) != {
        "baseline_sha", "command", "exit", "fixture_digest", "fixture_files",
        "fixture_patch_sha256", "kind", "schema", "staged_write_tree", "task",
    } or value.get("schema") != 1 or value.get("task") != 8 or value.get("exit") != 86:
        raise QaError("task-8 RED linkage identity mismatch")
    tree = git_run(root, "rev-parse", f"{sha}^{{tree}}", text=True).stdout.strip()
    if (
        value.get("baseline_sha") != sha
        or value.get("staged_write_tree") != tree
        or value.get("kind") != "development-red"
        or value.get("command") != entry["command"]
    ):
        raise QaError("task-8 RED linkage receiving mismatch")
    records = value.get("fixture_files")
    if not isinstance(records, list) or [record.get("path") for record in records] != list(RED_PATHS):
        raise QaError("task-8 RED fixture set mismatch")
    for record in records:
        relative = record.get("path")
        if not isinstance(relative, str) or record.get("sha256") != hashlib.sha256(
            git_blob(root, sha, relative)
        ).hexdigest():
            raise QaError("task-8 RED fixture linkage mismatch")
    fixture_digest = hashlib.sha256(
        "".join(f"{record['sha256']}  {record['path']}\n" for record in records).encode()
    ).hexdigest()
    patch = git_run(root, "diff", "--cached", "--binary", "HEAD", "--", *RED_PATHS).stdout
    if value["fixture_digest"] != fixture_digest or value["fixture_patch_sha256"] != hashlib.sha256(patch).hexdigest():
        raise QaError("task-8 RED aggregate linkage mismatch")


def content_digest(root: Path, sha: str, plan_sha: str) -> str:
    digest = hashlib.sha256()
    for relative in CONTENT_PATHS:
        digest.update(relative.encode() + b"\0" + git_blob(root, sha, relative))
    digest.update(b"publication\0" + json.dumps(PUBLICATION, sort_keys=True, separators=(",", ":")).encode())
    digest.update(b"normalized_plan_sha256\0" + plan_sha.encode())
    return digest.hexdigest()


def seal_locked(root: Path, receipt: Path, sha: str, directory: int) -> str:
    if sha != git_run(root, "rev-parse", "HEAD", text=True).stdout.strip():
        raise QaError("task-8 receiving SHA drift")
    plan_sha = validate_context(root, receipt)
    scenario = scenario_document(root)
    reject_replace_refs(root)
    receiving_status = git_run(
        root, "status", "--porcelain=v1", "-z", "--untracked-files=all"
    ).stdout
    exits: list[JsonValue] = []
    with tempfile.TemporaryDirectory(prefix="mlc-task8-authoritative-") as temporary:
        evidence = Path(temporary)
        red = scenario["red"][0]
        verify_red(root, evidence / "red", red, sha)
        exits.append({"command": red["command"], "exit": 86})
        for bucket in ("happy", "failure"):
            entry = scenario[bucket][0]
            command, result = exact_run(root, entry, evidence / bucket)
            exits.append({"command": command, "exit": result})
    current_sha = git_run(root, "rev-parse", "HEAD", text=True).stdout.strip()
    if current_sha != sha:
        raise QaError("task-8 HEAD changed during scenarios")
    if os.environ.get("MLC_TASK8_POST_USE_INJECTION") == "receiving-untracked":
        (root / "task8-unexpected-post-use").write_text("injected\n")
    reject_replace_refs(root)
    status = git_run(
        root, "status", "--porcelain=v1", "-z", "--untracked-files=all"
    ).stdout
    if status != receiving_status:
        raise QaError("task-8 receiving status drift after scenarios")
    plan_sha = validate_context(root, receipt)
    tree = git_run(root, "rev-parse", f"{sha}^{{tree}}", text=True).stdout.strip()
    commands = {bucket: [scenario[bucket][0]["command"]] for bucket in ("red", "happy", "failure")}
    value: dict[str, JsonValue] = {
        "content_sha256": content_digest(root, sha, plan_sha),
        "normalized_plan_sha256": plan_sha,
        "publication": PUBLICATION,
        "qemu": "deferred_unexecuted_baseline_link_failure",
        "receiving_sha": sha,
        "receiving_tree": tree,
        "scenario_commands": commands,
        "scenario_exits": exits,
        "scenario_sha256": hashlib.sha256(git_blob(root, sha, SCENARIO)).hexdigest(),
        "schema": 2,
        "status": "host_scenarios_sealed_qemu_explicitly_deferred",
        "task": 8,
        "threat_model": THREAT_MODEL,
    }
    payload = (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()
    previous = os.environ.get("MLC_AUTHORITATIVE_TASK")
    os.environ["MLC_AUTHORITATIVE_TASK"] = "8"
    try:
        publish(root, receipt, payload, plan_sha, directory)
    finally:
        if previous is None:
            os.environ.pop("MLC_AUTHORITATIVE_TASK", None)
        else:
            os.environ["MLC_AUTHORITATIVE_TASK"] = previous
    return hashlib.sha256(payload).hexdigest()


def seal(root: Path, receipt: Path, sha: str) -> str:
    if not receipt.is_absolute() or root not in receipt.parents:
        raise QaError("task-8 receipt path escapes the receiving worktree")
    validate_context(root, receipt)
    relative = receipt.relative_to(root)
    directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
    try:
        for component in relative.parent.parts:
            try:
                child = os.open(
                    component,
                    os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
                    dir_fd=directory,
                )
            except FileNotFoundError:
                try:
                    os.mkdir(component, mode=0o700, dir_fd=directory)
                    os.fsync(directory)
                except FileExistsError:
                    pass
                child = os.open(
                    component,
                    os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
                    dir_fd=directory,
                )
            os.close(directory)
            directory = child
        # Task-8 RED linkage briefly exercises a private Git index transaction.
        # Serialize the complete seal lifecycle so concurrent sealers cannot
        # contend on index.lock before reaching the durable publication phase.
        fcntl.flock(directory, fcntl.LOCK_EX)
        return seal_locked(root, receipt, sha, directory)
    finally:
        os.close(directory)


def main() -> int:
    if len(sys.argv) != 4:
        return 64
    root = Path(sys.argv[1]).resolve(strict=True)
    receipt = Path(sys.argv[2]).resolve(strict=False)
    try:
        print(seal(root, receipt, sys.argv[3]))
    except (
        QaError,
        Task8BootstrapError,
        OSError,
        subprocess.CalledProcessError,
        json.JSONDecodeError,
    ) as error:
        print(f"task-8 authoritative seal failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
