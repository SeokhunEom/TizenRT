#!/usr/bin/env bash
set -euo pipefail

PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export PATH
while IFS= read -r hostile_name; do
	case "$hostile_name" in
		BASH_ENV|ENV|PYTHON*|GIT_*|\
		DYLD_*|LD_*|CPATH|C_INCLUDE_PATH|CPLUS_INCLUDE_PATH|LIBRARY_PATH|SDKROOT|\
		CC|CXX|CPP|CFLAGS|CXXFLAGS|CPPFLAGS|LDFLAGS|AR|AS|NM|OBJCOPY|OBJDUMP|\
		RANLIB|STRIP|COMPILER_PATH|GCC_EXEC_PREFIX|DEVELOPER_DIR|\
		MACOSX_DEPLOYMENT_TARGET|PKG_CONFIG_PATH|PKG_CONFIG_LIBDIR) unset "$hostile_name" ;;
	esac
done < <(compgen -v)
export PYTHONDONTWRITEBYTECODE=1 GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 \
	GIT_NO_REPLACE_OBJECTS=1
git_bin=/usr/bin/git
python_bin=/opt/homebrew/bin/python3

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
scenario="$repo_root/tools/mem_leak_checker_scenarios/task-5.json"
evidence="$repo_root/.omo/start-work/artifacts/task-5-executor"
task5_temp_dirs=()
task5_cleanup_done=0

cleanup_temp_dirs() {
	cleanup_status=$?
	local temp_dir
	trap - EXIT HUP INT TERM
	[ "$task5_cleanup_done" = 0 ] || exit "$cleanup_status"
	task5_cleanup_done=1

	for temp_dir in ${task5_temp_dirs[@]+"${task5_temp_dirs[@]}"}; do
		case "$temp_dir" in
			/tmp/mlc-task5-*|/private/tmp/mlc-task5-*)
				[ ! -e "$temp_dir" ] || chmod -R u+w "$temp_dir" 2>/dev/null || true
				[ ! -e "$temp_dir" ] || find "$temp_dir" -depth -delete
				;;
		esac
	done
	exit "$cleanup_status"
}

trap cleanup_temp_dirs EXIT
trap 'trap - HUP INT TERM; exit 129' HUP
trap 'trap - HUP INT TERM; exit 130' INT
trap 'trap - HUP INT TERM; exit 143' TERM

fail() {
	printf '%s\n' "mem-leak-checker Task5 QA failed: $*" >&2
	exit 1
}

value_after() {
	name=$1
	shift
	while [ "$#" -gt 0 ]; do
		if [ "$1" = "$name" ]; then
			[ "$#" -ge 2 ] || fail "missing value for $name"
			printf '%s\n' "$2"
			return
		fi
		shift
	done
	fail "missing required option $name"
}

validate_context() {
	$python_bin -I -B - "$repo_root" "$scenario" "${MLC_TASK5_CONTEXT_MUTATION:-}" <<'PY'
import copy
import json
import os
import pathlib
import stat
import subprocess
import sys

root = pathlib.Path(sys.argv[1]).resolve()
scenario_path = pathlib.Path(sys.argv[2]).resolve()
mutation = sys.argv[3]
work_id = "mem-leak-checker-hardening-257754dc"
session = "codex:257754dc-1fe6-461f-86d4-57a7d2b9fb8d"
def read_regular(relative):
    candidate = pathlib.PurePosixPath(relative)
    directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
    try:
        for component in candidate.parent.parts:
            child = os.open(
                component,
                os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
                dir_fd=directory,
            )
            os.close(directory)
            directory = child
        descriptor = os.open(
            candidate.name,
            os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0),
            dir_fd=directory,
        )
        try:
            if not stat.S_ISREG(os.fstat(descriptor).st_mode):
                raise SystemExit(f"Task5 context path is not regular: {relative}")
            chunks = []
            while chunk := os.read(descriptor, 65536):
                chunks.append(chunk)
            return b"".join(chunks)
        finally:
            os.close(descriptor)
    finally:
        os.close(directory)

boulder = json.loads(read_regular(".omo/boulder.json"))
expected_work = {
    "work_id": work_id,
    "active_plan": ".omo/plans/mem-leak-checker-hardening.md",
    "plan_name": "mem-leak-checker-hardening",
    "session_ids": [session],
    "status": "active",
    "worktree_path": str(root),
}

if mutation == "boulder":
    boulder = copy.deepcopy(boulder)
    boulder["works"][work_id]["worktree_path"] = "/wrong"
if boulder != {
    "schema_version": 2,
    "active_work_id": work_id,
    "works": {work_id: expected_work},
}:
    raise SystemExit("Boulder receiving context mismatch")
if not (root / expected_work["active_plan"]).is_file():
    raise SystemExit("Task5 active plan is missing")
if subprocess.run(
    ["/usr/bin/git", "branch", "--show-current"], cwd=root, text=True,
    capture_output=True, check=True
).stdout.strip() != "codex/mem-leak-checker-hardening":
    raise SystemExit("Task5 receiving branch mismatch")
if subprocess.run(
    ["/usr/bin/git", "rev-parse", "--show-toplevel"], cwd=root, text=True,
    capture_output=True, check=True
).stdout.strip() != str(root):
    raise SystemExit("Task5 receiving worktree root mismatch")
if scenario_path != root / "tools/mem_leak_checker_scenarios/task-5.json":
    raise SystemExit("Task5 scenario path escaped the receiving worktree")
value = json.loads(read_regular("tools/mem_leak_checker_scenarios/task-5.json"))
if mutation in {"task", "fixture", "exit"}:
    value = copy.deepcopy(value)
    if mutation == "task":
        value["task"] = 4
    elif mutation == "fixture":
        value["scenarios"][0]["fixtures"] = ["wrong"]
    else:
        value["red"]["expected_exit"] = 0
if set(value) != {"schema", "task", "qemu", "red", "scenarios"}:
    raise SystemExit("Task5 scenario schema mismatch")
if value["schema"] != 1 or value["task"] != 5:
    raise SystemExit("Task5 scenario identity mismatch")
if value["qemu"] != "deferred_unexecuted_baseline_link_failure":
    raise SystemExit("Task5 scenario QEMU status mismatch")
if value["red"] != {
    "fixture": "mlc_lifecycle",
    "command": "tools/mem_leak_checker_qa.sh red --task 5 --config qemu/tc_1m --fixture mlc_lifecycle",
    "expected_exit": 86,
    "expected_records": ["MLC_TASK5_RED stale_identity_detected=true expected_exit=86"],
}:
    raise SystemExit("Task5 RED scenario mismatch")
expected = {
    "happy": [
        "mlc_lifecycle", "mlc_try_critical_fresh_variants",
        "mlc_fake_operation_budget",
    ],
    "failure": [
        "mlc_lifecycle_recoverable_faults", "mlc_critical_primary_busy",
        "mlc_critical_secondary_busy", "mlc_critical_preowned",
        "mlc_fake_deadline_reserve", "mlc_post_release_record_lifetime",
    ],
    "fatal": ["mlc_fatal_stub_isolated", "mlc_fatal_terminal_record"],
}
expected_commands = {
    "happy": "tools/mem_leak_checker_qa.sh qemu --task 5 --fixtures mlc_lifecycle,mlc_try_critical_fresh_variants,mlc_fake_operation_budget --repeat 100",
    "failure": "tools/mem_leak_checker_qa.sh qemu --task 5 --fixtures mlc_lifecycle_recoverable_faults,mlc_critical_primary_busy,mlc_critical_secondary_busy,mlc_critical_preowned,mlc_fake_deadline_reserve,mlc_post_release_record_lifetime --repeat 100",
    "fatal": "tools/mem_leak_checker_task5_qa.sh $MLC_TASK5_ARTIFACT_DIR",
}
if len(value["scenarios"]) != 3:
    raise SystemExit("Task5 scenario cardinality mismatch")
for item in value["scenarios"]:
    kind = item.get("kind")
    if kind not in expected or item.get("fixtures") != expected[kind]:
        raise SystemExit("Task5 scenario fixture mismatch")
    if item.get("command") != expected_commands[kind]:
        raise SystemExit("Task5 scenario command mismatch")
    if item.get("expected_exit") != 0 or not item.get("expected_records"):
        raise SystemExit("Task5 scenario result contract mismatch")
PY
}

validate_seal_worktree() {
	$python_bin -I -B - "$1" "$2" "$3" <<'PY'
import hashlib
import json
import os
import pathlib
import re
import stat
import subprocess
import sys

root = pathlib.Path(sys.argv[1]).resolve()
receipt_relative = sys.argv[2]
seal_task = int(sys.argv[3])
work_id = "mem-leak-checker-hardening-257754dc"
session = "codex:257754dc-1fe6-461f-86d4-57a7d2b9fb8d"
plan_relative = ".omo/plans/mem-leak-checker-hardening.md"
ledger_relative = ".omo/start-work/ledger.jsonl"
boulder_relative = ".omo/boulder.json"
expected_normalized_plan_digest = "47d8f48a15eaae0f390737bcd3e488948022e3780dcedcf77445de13003f1c93"
allowlist_relative = "tools/mem_leak_checker_task5_seal_allowlist.json"
known_baseline = "c93078ab05bb6463467669fb6ee19bb75ee7eaba"

def git(*arguments, check=True):
    return subprocess.run(
        ["/usr/bin/git", "-C", str(root), *arguments], check=check,
        capture_output=True,
    )

def read_regular(relative):
    candidate = pathlib.PurePosixPath(relative)
    if candidate.is_absolute() or str(candidate) != relative or not candidate.parts:
        raise SystemExit(f"Task5 unsafe authenticated path: {relative!r}")
    if any(part in {"", ".", ".."} for part in candidate.parts):
        raise SystemExit(f"Task5 unsafe authenticated path: {relative!r}")
    directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
    try:
        for component in candidate.parent.parts:
            child = os.open(
                component,
                os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
                dir_fd=directory,
            )
            os.close(directory)
            directory = child
        descriptor = os.open(
            candidate.name,
            os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0),
            dir_fd=directory,
        )
        try:
            mode = os.fstat(descriptor).st_mode
            if not stat.S_ISREG(mode):
                raise SystemExit(f"Task5 authenticated path is not regular: {relative!r}")
            chunks = []
            while chunk := os.read(descriptor, 65536):
                chunks.append(chunk)
            return b"".join(chunks)
        finally:
            os.close(descriptor)
    finally:
        os.close(directory)

head = git("rev-parse", "HEAD").stdout.decode().strip()
expected_task4_receipt = (
    ".omo/start-work/artifacts/task-4-executor/"
    f"task-4-post-integration-{head}.json"
)
expected_task5_receipt = (
    ".omo/start-work/artifacts/task-5-executor/"
    f"task-5-post-integration-{head}.json"
)
expected_task8_receipt = (
    ".omo/start-work/artifacts/task-8-executor/"
    f"task-8-post-integration-{head}.json"
)
expected_prospective = {
    4: expected_task4_receipt,
    5: expected_task5_receipt,
    8: expected_task8_receipt,
}
if seal_task not in expected_prospective or receipt_relative != expected_prospective[seal_task]:
    raise SystemExit("Task5 receipt path is not the exact mode-owned current-HEAD path")

if git("diff", "--quiet", "--", check=False).returncode != 0:
    raise SystemExit("Task5 tracked receiving worktree is dirty")
if git("diff", "--cached", "--quiet", "--", check=False).returncode != 0:
    raise SystemExit("Task5 receiving index is dirty")
if os.environ.get("MLC_TASK5_AFTER_CLEAN_MUTATION") == "1":
    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
        raise SystemExit("Task5 cleanliness mutation escaped its private fixture")
    with (root / "os/kernel/irq/irq_csection.c").open("ab") as stream:
        stream.write(b"after-clean mutation\n")

def safe_regular(relative):
    candidate = pathlib.PurePosixPath(relative)
    if candidate.is_absolute() or str(candidate) != relative:
        raise SystemExit(f"Task5 unsafe untracked path: {relative!r}")
    if not candidate.parts or any(part in {"", ".", ".."} for part in candidate.parts):
        raise SystemExit(f"Task5 unsafe untracked path: {relative!r}")
    current = root
    for index, component in enumerate(candidate.parts):
        current = current / component
        try:
            mode = current.lstat().st_mode
        except FileNotFoundError:
            raise SystemExit(f"Task5 untracked path disappeared: {relative!r}")
        if stat.S_ISLNK(mode):
            raise SystemExit(f"Task5 symlink is forbidden: {relative!r}")
        if index + 1 < len(candidate.parts) and not stat.S_ISDIR(mode):
            raise SystemExit(f"Task5 non-directory path component: {relative!r}")
    if not stat.S_ISREG(mode):
        raise SystemExit(f"Task5 untracked state must be a regular file: {relative!r}")

boulder_bytes = read_regular(boulder_relative)
plan_bytes = read_regular(plan_relative)
if os.environ.get("MLC_TASK5_BETWEEN_READS_MUTATION") == "1":
    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
        raise SystemExit("Task5 read mutation escaped its private fixture")
    with (root / "os/kernel/irq/irq_csection.c").open("ab") as stream:
        stream.write(b"between-reads mutation\n")
ledger_bytes = read_regular(ledger_relative)
boulder = json.loads(boulder_bytes)
expected_work = {
    "work_id": work_id,
    "active_plan": plan_relative,
    "plan_name": "mem-leak-checker-hardening",
    "session_ids": [session],
    "status": "active",
    "worktree_path": str(root),
}
if boulder != {
    "schema_version": 2,
    "active_work_id": work_id,
    "works": {work_id: expected_work},
}:
    raise SystemExit("Task5 Boulder receiving context mismatch")

ledger_lines = ledger_bytes.decode().splitlines()
if not ledger_lines:
    raise SystemExit("Task5 orchestration ledger is empty")
try:
    ledger = [json.loads(line) for line in ledger_lines]
except json.JSONDecodeError as error:
    raise SystemExit(f"Task5 orchestration ledger is malformed: {error}")
first = ledger[0]
if (
    first.get("event") != "work-started"
    or first.get("task") != "orchestration"
    or first.get("session_id") != session
    or first.get("plan") != plan_relative
    or first.get("artifact") != boulder_relative
):
    raise SystemExit("Task5 orchestration ledger identity mismatch")
baseline = first.get("baseline_sha", "")
if baseline != known_baseline:
    raise SystemExit("Task5 orchestration baseline mismatch")
normalized_plan = re.sub(
    rb"(?m)^- \[(?: |x)\] (?=(?:[0-9]+|F[1-4])\.)",
    b"- [ ] ",
    plan_bytes,
)
normalized_plan_digest = hashlib.sha256(normalized_plan).hexdigest()
if normalized_plan_digest != expected_normalized_plan_digest:
    raise SystemExit("Task5 active plan content drift")
if first.get("plan_sha256") != hashlib.sha256(plan_bytes).hexdigest():
    sanctioned_plan_updates = {
        ".omo/plans/mem-leak-checker-hardening.md#approved-execution-override-2026-07-19",
        ".omo/plans/mem-leak-checker-hardening.md#resumed-worktree-and-evidence-filesystem-exception",
    }
    recorded_updates = {
        entry.get("artifact") for entry in ledger
        if entry.get("event") in {"execution-override", "execution-exception"}
    }
    plan_text = plan_bytes.decode()
    if not sanctioned_plan_updates <= recorded_updates or not all(
        heading in plan_text for heading in (
            "### Approved execution override (2026-07-19)",
            "### Resumed-worktree and evidence-filesystem exception",
        )
    ):
        raise SystemExit("Task5 active plan digest mismatch")
for entry in ledger:
    if entry.get("session_id") != session or entry.get("plan") != plan_relative:
        raise SystemExit("Task5 orchestration ledger session or plan drift")
if git("merge-base", "--is-ancestor", baseline, "HEAD", check=False).returncode != 0:
    raise SystemExit("Task5 orchestration baseline is not an ancestor of HEAD")

baseline_relative = ".omo/start-work/artifacts/task-1-executor/baseline.sha"
if read_regular(baseline_relative) != (known_baseline + "\n").encode():
    raise SystemExit("Task5 durable baseline file mismatch")

allowlist_blob = git("show", f"{head}:{allowlist_relative}").stdout
allowlist = json.loads(allowlist_blob)
if set(allowlist) != {"schema", "task", "files"}:
    raise SystemExit("Task5 seal allowlist schema mismatch")
if allowlist["schema"] != 1 or allowlist["task"] != 5:
    raise SystemExit("Task5 seal allowlist identity mismatch")
allowed_artifacts = {}
for entry in allowlist["files"]:
    if set(entry) != {"path", "sha256"}:
        raise SystemExit("Task5 seal allowlist entry schema mismatch")
    relative = entry["path"]
    digest = entry["sha256"]
    if not re.fullmatch(
        r"\.omo/start-work/(?:artifacts/task-[1-5]-executor/.+|"
        r"recovery/preintegrate-todo[1-5]-stale/.+)", relative
    ):
        raise SystemExit(f"Task5 invalid allowlist path: {relative!r}")
    if re.fullmatch(
        r"\.omo/start-work/artifacts/task-([458])-executor/"
        r"task-\1-post-integration-[0-9a-f]{40}\.json",
        relative,
    ):
        raise SystemExit(f"Task5 versioned receipt cannot be manifest-authorized: {relative!r}")
    if relative in {expected_task4_receipt, expected_task5_receipt} or relative in allowed_artifacts:
        raise SystemExit(f"Task5 duplicate or prospective receipt allowlist path: {relative!r}")
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise SystemExit(f"Task5 invalid allowlist digest: {relative!r}")
    if hashlib.sha256(read_regular(relative)).hexdigest() != digest:
        raise SystemExit(f"Task5 authenticated artifact content drift: {relative!r}")
    if git("ls-files", "--error-unmatch", "--", relative, check=False).returncode == 0:
        raise SystemExit(f"Task5 orchestration artifact became tracked: {relative!r}")
    allowed_artifacts[relative] = digest
if baseline_relative not in allowed_artifacts:
    raise SystemExit("Task5 durable baseline is absent from the authenticated allowlist")

untracked_output = git("ls-files", "--others", "-z").stdout
untracked_paths = [os.fsdecode(raw) for raw in untracked_output.split(b"\0") if raw]
if seal_task == 5 and expected_task4_receipt not in untracked_paths:
    raise SystemExit("Task5 seal requires the exact current-HEAD Task4 receipt")
external_root = root / ".codegraph"
try:
    external_mode = external_root.lstat().st_mode
except FileNotFoundError:
    raise SystemExit("Task5 live external Codegraph directory is absent")
if stat.S_ISLNK(external_mode) or not stat.S_ISDIR(external_mode):
    raise SystemExit("Task5 live external Codegraph root is not a real directory")
if external_root.resolve() != root / ".codegraph":
    raise SystemExit("Task5 live external Codegraph root escapes the worktree")
if git("ls-files", "-z", "--", ".codegraph").stdout:
    raise SystemExit("Task5 live external Codegraph subtree contains tracked content")

external_paths = []
for relative in untracked_paths:
    if not relative.startswith(".codegraph/"):
        continue
    candidate = pathlib.PurePosixPath(relative)
    if candidate.parts[0] != ".codegraph" or any(
        part in {"", ".", ".."} for part in candidate.parts
    ):
        raise SystemExit(f"Task5 unsafe live external Codegraph path: {relative!r}")
    current = root
    for index, component in enumerate(candidate.parts):
        current = current / component
        mode = current.lstat().st_mode
        if stat.S_ISLNK(mode):
            raise SystemExit(f"Task5 live external Codegraph symlink is forbidden: {relative!r}")
        if index + 1 < len(candidate.parts) and not stat.S_ISDIR(mode):
            raise SystemExit(f"Task5 live external Codegraph path component is unsafe: {relative!r}")
    if not stat.S_ISREG(mode):
        raise SystemExit(f"Task5 live external Codegraph entry is not regular: {relative!r}")
    if current.resolve().relative_to(root) != candidate:
        raise SystemExit(f"Task5 live external Codegraph path escapes the worktree: {relative!r}")
    if git("check-ignore", "--quiet", "--", relative, check=False).returncode != 0:
        raise SystemExit(f"Task5 live external Codegraph entry is not ignored: {relative!r}")
    external_paths.append(relative)
if not external_paths:
    raise SystemExit("Task5 live external Codegraph subtree has no enumerated ignored files")

for relative in untracked_paths:
    allowed = relative in {
        boulder_relative,
        plan_relative,
        ledger_relative,
        expected_task4_receipt,
        expected_task5_receipt,
        expected_task8_receipt,
    }
    allowed = allowed or relative in allowed_artifacts
    allowed = allowed or relative in external_paths
    if not allowed:
        raise SystemExit(f"Task5 unsanctioned untracked path: {relative!r}")
    safe_regular(relative)

publication = {
    "atomic_visibility": False,
    "concurrency_scope": "cooperating_sealers_only",
    "cryptographic_authentication": False,
    "directory_fsync": True,
    "external_writer_authenticated": False,
    "external_writer_mutation_scope": "detected_where_observed_otherwise_outside_receipt_trust_boundary",
    "file_fsync": True,
    "idempotent_replay_fsync": True,
    "immutable": False,
    "mac_authenticated": False,
    "mode": "exclusive_final_inode_weaker_exfat",
    "reopened_named_payload_fsync": True,
    "surviving_identity_validation": "original_descriptor_or_reopened_exact_named_payload",
}
for task, relative in (
    (4, expected_task4_receipt),
    (5, expected_task5_receipt),
    (8, expected_task8_receipt),
):
    if relative not in untracked_paths:
        continue
    value = json.loads(read_regular(relative))
    common = {
        "schema", "task", "status", "receiving_sha", "receiving_tree",
        "content_sha256", "scenario_sha256", "normalized_plan_sha256",
        "scenario_exits", "qemu", "publication",
    }
    if task == 5:
        task_specific = {"external_state_exclusions", "scenario_commands"}
    elif task == 8:
        task_specific = {"scenario_commands", "threat_model"}
    else:
        task_specific = {"scenario_commands"}
    if set(value) != common | task_specific:
        raise SystemExit(f"Task5 versioned Task{task} receipt schema mismatch")
    if value["schema"] != 2 or value["task"] != task:
        raise SystemExit(f"Task5 versioned Task{task} receipt identity mismatch")
    if value["receiving_sha"] != head or value["receiving_tree"] != git("rev-parse", "HEAD^{tree}").stdout.decode().strip():
        raise SystemExit(f"Task5 versioned Task{task} receiving identity mismatch")
    if not all(
        isinstance(value[field], str) and re.fullmatch(r"[0-9a-f]{64}", value[field])
        for field in ("content_sha256", "scenario_sha256", "normalized_plan_sha256")
    ):
        raise SystemExit(f"Task5 versioned Task{task} digest malformed")
    scenario_relative = f"tools/mem_leak_checker_scenarios/task-{task}.json"
    scenario_blob = git("show", f"{head}:{scenario_relative}").stdout
    if value["scenario_sha256"] != hashlib.sha256(scenario_blob).hexdigest():
        raise SystemExit(f"Task5 versioned Task{task} scenario digest mismatch")
    if value["normalized_plan_sha256"] != normalized_plan_digest:
        raise SystemExit(f"Task5 versioned Task{task} plan digest mismatch")
    if value["publication"] != publication:
        raise SystemExit(f"Task5 versioned Task{task} publication mismatch")
    if value["qemu"] != "deferred_unexecuted_baseline_link_failure":
        raise SystemExit(f"Task5 versioned Task{task} QEMU status mismatch")
    if task == 4:
        content_paths = (
            "os/kernel/debug/mem_leak_checker_graph.c",
            "os/kernel/debug/mem_leak_checker_graph.h",
            "os/kernel/debug/mem_leak_checker_graph_internal.h",
            "os/kernel/debug/mem_leak_checker_graph_validate.c",
            "os/kernel/debug/tests/test_mem_leak_checker_graph.c",
            "os/kernel/debug/tests/test_mem_leak_checker_graph_depth.c",
            "tools/mem_leak_checker_qa.sh",
            "tools/mem_leak_checker_qa_core.py",
            "tools/mem_leak_checker_scenarios/task-4.json",
            "tools/mem_leak_checker_task4_authoritative.py",
            "tools/mem_leak_checker_task4_evidence.py",
            "tools/mem_leak_checker_task4_qa.sh",
            "tools/mem_leak_checker_task4_scenarios.py",
            "tools/test_mem_leak_checker_graph.sh",
        )
        scenario = json.loads(scenario_blob)
        expected_commands = {
            bucket: [entry["command"] for entry in scenario[bucket]]
            for bucket in ("red", "happy", "failure")
        }
        expected_exits = [
            {"command": entry["command"], "exit": 0 if entry.get("expected_exit") is None else entry["expected_exit"]}
            for bucket in ("red", "happy", "failure") for entry in scenario[bucket]
        ]
        if value["scenario_commands"] != expected_commands or value["scenario_exits"] != expected_exits:
            raise SystemExit("Task5 versioned Task4 command or exit mismatch")
        if value["status"] != "host_scenarios_sealed_qemu_explicitly_deferred":
            raise SystemExit("Task5 versioned Task4 status mismatch")
    elif task == 5:
        content_paths = (
            "os/kernel/irq/irq_csection.c",
            "os/kernel/debug/mem_leak_checker.c",
            "os/kernel/debug/mem_leak_checker_lifecycle.c",
            "os/kernel/debug/mem_leak_checker_lifecycle.h",
            "tools/mem_leak_checker_task5_model.c",
            "tools/mem_leak_checker_task5_lifecycle_test.c",
            "tools/mem_leak_checker_task5_irq_actual_test.c",
            "tools/mem_leak_checker_task5_irq_fallback_test.c",
            "tools/mem_leak_checker_task5_qa.sh",
            "tools/mem_leak_checker_task5_scenarios.sh",
            "tools/mem_leak_checker_task5_seal_test.sh",
            "tools/mem_leak_checker_task5_seal_allowlist.json",
            "tools/mem_leak_checker_qa.sh",
            "tools/mem_leak_checker_task5_stubs/arch/irq.h",
            "tools/mem_leak_checker_scenarios/task-5.json",
        )
        expected_exclusions = [{
            "path": ".codegraph", "reason": "live_external_codegraph_mcp", "authenticated": False,
        }]
        scenario = json.loads(scenario_blob)
        expected_commands = {
            item["kind"]: item["command"] for item in scenario["scenarios"]
        }
        if (
            value["status"] != "host_scenarios_sealed_qemu_explicitly_deferred"
            or value["scenario_exits"] != {"happy": 0, "failure": 0, "fatal": 0}
            or value["scenario_commands"] != expected_commands
            or value["external_state_exclusions"] != expected_exclusions
        ):
            raise SystemExit("Task5 versioned Task5 scenario or exclusion mismatch")
    else:
        expected_threat_model = {
            "active_same_uid_race": "excluded",
            "checked_inputs": "postvalidated",
            "trusted_initial_root": "tools/mem_leak_checker_task8_qa.sh_body_before_python_bootstrap_cannot_self_authenticate",
            "shell_startup_before_script_body": "excluded",
            "scope": "cooperative_local_qa",
            "trusted_entrypoint_bootstrap": "required_before_validation",
        }
        content_paths = (
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
        scenario = json.loads(scenario_blob)
        expected_commands = {
            bucket: [entry["command"] for entry in scenario[bucket]]
            for bucket in ("red", "happy", "failure")
        }
        expected_exits = [
            {"command": entry["command"], "exit": entry["expected_exit"]}
            for bucket in ("red", "happy", "failure")
            for entry in scenario[bucket]
        ]
        if (
            value["status"] != "host_scenarios_sealed_qemu_explicitly_deferred"
            or value["scenario_commands"] != expected_commands
            or value["scenario_exits"] != expected_exits
            or value["threat_model"] != expected_threat_model
        ):
            raise SystemExit("Task5 versioned Task8 scenario mismatch")
    content = hashlib.sha256()
    for content_path in content_paths:
        content.update(content_path.encode() + b"\0" + git("show", f"{head}:{content_path}").stdout)
    if task == 5:
        content.update(
            b"external_state_exclusions\0" +
            json.dumps(expected_exclusions, sort_keys=True, separators=(",", ":")).encode()
        )
    content.update(
        b"publication\0" + json.dumps(publication, sort_keys=True, separators=(",", ":")).encode()
    )
    content.update(b"normalized_plan_sha256\0" + normalized_plan_digest.encode())
    if value["content_sha256"] != content.hexdigest():
        raise SystemExit(f"Task5 versioned Task{task} content digest mismatch")
print(normalized_plan_digest)
PY
}

validate_receipt_file() {
	$python_bin -I -B - "$1" "$2" "$3" "$4" "$5" "$6" <<'PY'
import json
import os
import pathlib
import stat
import sys

path = pathlib.Path(sys.argv[1])
receiving_sha, receiving_tree, content_sha, scenario_sha, plan_sha = sys.argv[2:]
if not path.is_absolute():
    raise SystemExit("Task5 receipt path must be absolute")
directory = os.open("/", os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
try:
    for component in path.parent.parts[1:]:
        child = os.open(
            component,
            os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
            dir_fd=directory,
        )
        os.close(directory)
        directory = child
    descriptor = os.open(
        path.name,
        os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0),
        dir_fd=directory,
    )
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise SystemExit("Task5 receipt is not regular")
        chunks = []
        while chunk := os.read(descriptor, 65536):
            chunks.append(chunk)
    finally:
        os.close(descriptor)
finally:
    os.close(directory)
value = json.loads(b"".join(chunks))
expected = {
    "schema": 2,
    "task": 5,
    "status": "host_scenarios_sealed_qemu_explicitly_deferred",
    "receiving_sha": receiving_sha,
    "receiving_tree": receiving_tree,
    "content_sha256": content_sha,
    "scenario_sha256": scenario_sha,
    "normalized_plan_sha256": plan_sha,
    "scenario_exits": {"happy": 0, "failure": 0, "fatal": 0},
    "scenario_commands": {
        "happy": "tools/mem_leak_checker_qa.sh qemu --task 5 --fixtures mlc_lifecycle,mlc_try_critical_fresh_variants,mlc_fake_operation_budget --repeat 100",
        "failure": "tools/mem_leak_checker_qa.sh qemu --task 5 --fixtures mlc_lifecycle_recoverable_faults,mlc_critical_primary_busy,mlc_critical_secondary_busy,mlc_critical_preowned,mlc_fake_deadline_reserve,mlc_post_release_record_lifetime --repeat 100",
        "fatal": "tools/mem_leak_checker_task5_qa.sh $MLC_TASK5_ARTIFACT_DIR",
    },
    "qemu": "deferred_unexecuted_baseline_link_failure",
    "external_state_exclusions": [{
        "path": ".codegraph",
        "reason": "live_external_codegraph_mcp",
        "authenticated": False,
    }],
    "publication": {
        "atomic_visibility": False,
        "concurrency_scope": "cooperating_sealers_only",
        "cryptographic_authentication": False,
        "directory_fsync": True,
        "external_writer_authenticated": False,
        "external_writer_mutation_scope": "detected_where_observed_otherwise_outside_receipt_trust_boundary",
        "file_fsync": True,
        "idempotent_replay_fsync": True,
        "immutable": False,
        "mac_authenticated": False,
        "mode": "exclusive_final_inode_weaker_exfat",
        "reopened_named_payload_fsync": True,
        "surviving_identity_validation": "original_descriptor_or_reopened_exact_named_payload",
    },
}
if value != expected:
    raise SystemExit("Task5 receipt content mismatch")
PY
}

kind_for_fixtures() {
	$python_bin -I -B - "$scenario" "$1" <<'PY'
import json
import sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
selected = sys.argv[2].split(",")
for item in value["scenarios"]:
    if item["fixtures"] == selected:
        print(item["kind"])
        break
else:
    raise SystemExit("Task5 fixture selection does not match a declared scenario")
PY
}

run_kind() {
	kind=$1
	artifact_dir=$(mktemp -d "/tmp/mlc-task5-${kind}.XXXXXX")
	task5_temp_dirs+=("$artifact_dir")
	output="$artifact_dir/output.log"
	"$repo_root/tools/mem_leak_checker_task5_qa.sh" "$artifact_dir" >"$output" 2>&1
	$python_bin -I -B - "$scenario" "$kind" "$output" <<'PY'
import json
import pathlib
import sys
scenario = json.load(open(sys.argv[1], encoding="utf-8"))
kind = sys.argv[2]
output = pathlib.Path(sys.argv[3]).read_text(encoding="utf-8")
item = next(entry for entry in scenario["scenarios"] if entry["kind"] == kind)
for record in item["expected_records"]:
    if record not in output:
        raise SystemExit(f"missing Task5 record: {record}")
print(output, end="")
PY
}

run_locked_seal() {
	$python_bin -I -B - "$repo_root" "$0" "$1" <<'PY'
import fcntl
import os
import pathlib
import subprocess
import sys
import time

root = pathlib.Path(sys.argv[1])
script = sys.argv[2]
source = sys.argv[3]
task4_relative = pathlib.PurePosixPath(".omo/start-work/artifacts/task-4-executor")
relative = pathlib.PurePosixPath(".omo/start-work/artifacts/task-5-executor")
directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
try:
    task4_directory = os.dup(directory)
    try:
        for component in task4_relative.parts:
            child = os.open(
                component,
                os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
                dir_fd=task4_directory,
            )
            os.close(task4_directory)
            task4_directory = child
        fcntl.flock(task4_directory, fcntl.LOCK_SH)
    except BaseException:
        os.close(task4_directory)
        raise
    for component in relative.parts:
        child = os.open(
            component,
            os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
            dir_fd=directory,
        )
        os.close(directory)
        directory = child
    barrier_value = os.environ.get("MLC_TASK5_LOCK_BARRIER_DIR")
    role = os.environ.get("MLC_TASK5_LOCK_ROLE")
    barrier = pathlib.Path(barrier_value).resolve() if barrier_value else None
    if barrier is not None:
        if role not in {"holder", "contender"}:
            raise SystemExit("Task5 lock barrier role is invalid")
        if not str(root.resolve()).startswith(
            ("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")
        ) or barrier.parent != root.resolve().parent:
            raise SystemExit("Task5 lock barrier escaped its private fixture")
        barrier.mkdir(mode=0o700, exist_ok=True)
        if role == "contender":
            (barrier / "contender.started").touch(exist_ok=False)
    fcntl.flock(directory, fcntl.LOCK_EX)
    if barrier is not None:
        (barrier / f"{role}.locked").touch(exist_ok=False)
        if role == "holder":
            deadline = time.monotonic() + 30
            while not (barrier / "holder.release").exists():
                if time.monotonic() >= deadline:
                    raise SystemExit("Task5 lock barrier timed out")
                time.sleep(0.001)
    os.set_inheritable(directory, True)
    os.set_inheritable(task4_directory, True)
    environment = {
        **os.environ,
        "MLC_TASK4_DEPENDENCY_LOCK_FD": str(task4_directory),
        "MLC_TASK5_LOCK_FD": str(directory),
    }
    completed = subprocess.run(
        [script, "seal", "--source", source],
        cwd=root,
        env=environment,
        pass_fds=(task4_directory, directory),
        check=False,
    )
    os.close(task4_directory)
    raise SystemExit(completed.returncode)
finally:
    os.close(directory)
PY
}

command=${1:-}
[ -n "$command" ] || fail "missing command"
shift

case "$command" in
	validate-receiving)
		validate_context
		[ "$#" -eq 4 ] || fail "unexpected receiving validation options"
		receipt_relative=$(value_after --receipt "$@")
		seal_task=$(value_after --task "$@")
		case "$seal_task" in 4|5|8) ;; *) fail "invalid receiving validation task" ;; esac
		validate_seal_worktree "$repo_root" "$receipt_relative" "$seal_task"
		;;
	red)
		validate_context
		config=$(value_after --config "$@")
		fixture=$(value_after --fixture "$@")
		[ "$config" = qemu/tc_1m ] || fail "Task5 RED config mismatch"
		[ "$fixture" = mlc_lifecycle ] || fail "Task5 RED fixture mismatch"
		artifact_dir=$(mktemp -d /tmp/mlc-task5-red.XXXXXX)
		task5_temp_dirs+=("$artifact_dir")
		set +e
		"$repo_root/tools/mem_leak_checker_task5_qa.sh" --red "$artifact_dir"
		red_exit=$?
		set -e
		[ "$red_exit" -eq 86 ] || fail "Task5 RED subprocess exit was $red_exit"
		printf 'MLC_QA_RED task=5 fixture=%s status=host_stale_identity_rejected exit=86 head=%s\n' \
			"$fixture" "$($git_bin -C "$repo_root" rev-parse HEAD)"
		exit 86
		;;
	qemu)
		validate_context
		fixtures=$(value_after --fixtures "$@")
		repeat=$(value_after --repeat "$@")
		[ "$repeat" = 100 ] || fail "Task5 repeat must be 100"
		post_commit=false
		for argument in "$@"; do
			[ "$argument" != --post-commit ] || post_commit=true
		done
		kind=$(kind_for_fixtures "$fixtures")
		run_kind "$kind"
		printf 'MLC_QA_QEMU task=5 kind=%s fixtures=%s repeat=100 post_commit=%s host_models=PASS qemu=deferred_unexecuted_baseline_link_failure\n' \
			"$kind" "$fixtures" "$post_commit"
		;;
	receipt-negative)
		validate_context
		for mutation in boulder task fixture exit; do
			if MLC_TASK5_CONTEXT_MUTATION=$mutation validate_context >/dev/null 2>&1; then
				fail "Task5 validator accepted $mutation mutation"
			fi
		done
		artifact_dir=$(mktemp -d /tmp/mlc-task5-receipt-negative.XXXXXX)
		artifact_dir=$(CDPATH= cd -- "$artifact_dir" && pwd -P)
		task5_temp_dirs+=("$artifact_dir")
		$python_bin -I -B - "$artifact_dir" <<'PY'
import copy
import json
import pathlib
import sys

directory = pathlib.Path(sys.argv[1])
canonical = {
    "schema": 2,
    "task": 5,
    "status": "host_scenarios_sealed_qemu_explicitly_deferred",
    "receiving_sha": "a" * 40,
    "receiving_tree": "b" * 40,
    "content_sha256": "c" * 64,
    "scenario_sha256": "d" * 64,
    "normalized_plan_sha256": "e" * 64,
    "scenario_exits": {"happy": 0, "failure": 0, "fatal": 0},
    "scenario_commands": {
        "happy": "tools/mem_leak_checker_qa.sh qemu --task 5 --fixtures mlc_lifecycle,mlc_try_critical_fresh_variants,mlc_fake_operation_budget --repeat 100",
        "failure": "tools/mem_leak_checker_qa.sh qemu --task 5 --fixtures mlc_lifecycle_recoverable_faults,mlc_critical_primary_busy,mlc_critical_secondary_busy,mlc_critical_preowned,mlc_fake_deadline_reserve,mlc_post_release_record_lifetime --repeat 100",
        "fatal": "tools/mem_leak_checker_task5_qa.sh $MLC_TASK5_ARTIFACT_DIR",
    },
    "qemu": "deferred_unexecuted_baseline_link_failure",
    "external_state_exclusions": [{
        "path": ".codegraph",
        "reason": "live_external_codegraph_mcp",
        "authenticated": False,
    }],
    "publication": {
        "atomic_visibility": False,
        "concurrency_scope": "cooperating_sealers_only",
        "cryptographic_authentication": False,
        "directory_fsync": True,
        "external_writer_authenticated": False,
        "external_writer_mutation_scope": "detected_where_observed_otherwise_outside_receipt_trust_boundary",
        "file_fsync": True,
        "idempotent_replay_fsync": True,
        "immutable": False,
        "mac_authenticated": False,
        "mode": "exclusive_final_inode_weaker_exfat",
        "reopened_named_payload_fsync": True,
        "surviving_identity_validation": "original_descriptor_or_reopened_exact_named_payload",
    },
}
(directory / "canonical.json").write_text(json.dumps(canonical))
for name, change in {
    "receipt-task": ("task", 4),
    "receipt-exit": ("scenario_exits", {"happy": 1, "failure": 0, "fatal": 0}),
    "receipt-command": ("scenario_commands", {}),
    "receipt-content": ("content_sha256", "e" * 64),
    "receipt-plan": ("normalized_plan_sha256", "f" * 64),
    "receipt-exclusion": ("external_state_exclusions", []),
    "receipt-concurrency": ("publication", {**canonical["publication"], "concurrency_scope": "all_writers"}),
    "receipt-external-writer": ("publication", {**canonical["publication"], "external_writer_authenticated": True}),
    "receipt-cryptographic": ("publication", {**canonical["publication"], "cryptographic_authentication": True}),
    "receipt-mutation-scope": ("publication", {**canonical["publication"], "external_writer_mutation_scope": "all_mutation_prevented"}),
    "receipt-mac": ("publication", {**canonical["publication"], "mac_authenticated": True}),
    "receipt-immutable": ("publication", {**canonical["publication"], "immutable": True}),
    "receipt-survivor-validation": ("publication", {**canonical["publication"], "surviving_identity_validation": "original_descriptor_only"}),
    "receipt-reopened-fsync": ("publication", {**canonical["publication"], "reopened_named_payload_fsync": False}),
    "receipt-replay-fsync": ("publication", {**canonical["publication"], "idempotent_replay_fsync": False}),
}.items():
    mutant = copy.deepcopy(canonical)
    mutant[change[0]] = change[1]
    (directory / f"{name}.json").write_text(json.dumps(mutant))
PY
		validate_receipt_file "$artifact_dir/canonical.json" \
			"$(printf 'a%.0s' {1..40})" "$(printf 'b%.0s' {1..40})" \
			"$(printf 'c%.0s' {1..64})" "$(printf 'd%.0s' {1..64})" \
			"$(printf 'e%.0s' {1..64})"
		for mutant in "$artifact_dir"/receipt-*.json; do
			mutant_sha=$(shasum -a 256 "$mutant" | awk '{print $1}')
			if validate_receipt_file "$mutant" \
				"$(printf 'a%.0s' {1..40})" "$(printf 'b%.0s' {1..40})" \
				"$(printf 'c%.0s' {1..64})" "$(printf 'd%.0s' {1..64})" \
				"$(printf 'e%.0s' {1..64})" \
				>/dev/null 2>&1; then
				fail "Task5 receipt validator accepted $(basename "$mutant")"
			fi
			[ "$mutant_sha" = "$(shasum -a 256 "$mutant" | awk '{print $1}')" ] || \
				fail "Task5 receipt validator replaced $(basename "$mutant")"
		done
		if [ "${MLC_TASK5_RECEIPT_FIXTURE_ONLY:-0}" = 1 ]; then
			case "$repo_root" in
			/tmp/mlc-task5-seal-test.*/repository|/private/tmp/mlc-task5-seal-test.*/repository) ;;
			*) fail "Task5 receipt fixture mode escaped its private test repository" ;;
			esac
		else
			"$repo_root/tools/mem_leak_checker_task5_seal_test.sh"
		fi
		printf '%s\n' 'MLC_QA_RECEIPT_NEGATIVE task=5 cases=boulder,task,fixture,exit,receipt_task,receipt_exit,receipt_content,receipt_plan,receipt_exclusion,receipt_concurrency,receipt_external_writer,receipt_cryptographic,receipt_mutation_scope,receipt_mac,receipt_immutable,receipt_survivor_validation,receipt_reopened_fsync,receipt_replay_fsync status=PASS'
		;;
	seal)
		source=$(value_after --source "$@")
		if [ -z "${MLC_TASK4_DEPENDENCY_LOCK_FD:-}" ] || [ -z "${MLC_TASK5_LOCK_FD:-}" ]; then
			run_locked_seal "$source"
			exit $?
		fi
		$python_bin -I -B - "$repo_root" "$MLC_TASK4_DEPENDENCY_LOCK_FD" "$MLC_TASK5_LOCK_FD" <<'PY'
import fcntl
import os
import pathlib
import stat
import sys

root = pathlib.Path(sys.argv[1])
for descriptor_value, relative, operation in (
    (sys.argv[2], ".omo/start-work/artifacts/task-4-executor", fcntl.LOCK_SH),
    (sys.argv[3], ".omo/start-work/artifacts/task-5-executor", fcntl.LOCK_EX),
):
    descriptor = os.dup(int(descriptor_value))
    try:
        locked = os.fstat(descriptor)
        named = os.stat(root / relative, follow_symlinks=False)
        if not stat.S_ISDIR(locked.st_mode) or (locked.st_dev, locked.st_ino) != (
            named.st_dev,
            named.st_ino,
        ):
            raise SystemExit("Task5 lifecycle lock directory identity mismatch")
        fcntl.flock(descriptor, operation)
    finally:
        os.close(descriptor)
PY
		validate_context
		receiving_sha=$($git_bin -C "$repo_root" rev-parse "$source")
		[ "$receiving_sha" = "$($git_bin -C "$repo_root" rev-parse HEAD)" ] || \
			fail "Task5 receiving source drift"
		receipt_relative=".omo/start-work/artifacts/task-5-executor/task-5-post-integration-$receiving_sha.json"
		plan_sha=$(validate_seal_worktree "$repo_root" "$receipt_relative" 5)
		receiving_tree=$($git_bin -C "$repo_root" rev-parse "$receiving_sha^{tree}")
		mkdir -p "$evidence"
		"$repo_root/tools/mem_leak_checker_qa.sh" qemu --task 5 \
			--fixtures mlc_lifecycle,mlc_try_critical_fresh_variants,mlc_fake_operation_budget \
			--repeat 100 >/dev/null
		"$repo_root/tools/mem_leak_checker_qa.sh" qemu --task 5 \
			--fixtures mlc_lifecycle_recoverable_faults,mlc_critical_primary_busy,mlc_critical_secondary_busy,mlc_critical_preowned,mlc_fake_deadline_reserve,mlc_post_release_record_lifetime \
			--repeat 100 >/dev/null
		fatal_artifact_dir=$(mktemp -d /tmp/mlc-task5-fatal.XXXXXX)
		task5_temp_dirs+=("$fatal_artifact_dir")
		MLC_TASK5_ARTIFACT_DIR="$fatal_artifact_dir" \
			"$repo_root/tools/mem_leak_checker_task5_qa.sh" "$fatal_artifact_dir" >/dev/null
		post_plan_sha=$(validate_seal_worktree "$repo_root" "$receipt_relative" 5)
		[ "$post_plan_sha" = "$plan_sha" ] || fail "Task5 receiving context changed during scenarios"
		[ "$($git_bin -C "$repo_root" rev-parse HEAD)" = "$receiving_sha" ] || fail "Task5 HEAD changed during scenarios"
		seal_values=$($python_bin -I -B - "$repo_root" "$scenario" \
			"$evidence/task-5-post-integration-$receiving_sha.json" "$receiving_sha" "$receiving_tree" \
			"$plan_sha" <<'PY'
import hashlib
import json
import os
import pathlib
import stat
import subprocess
import sys
root = pathlib.Path(sys.argv[1])
scenario_path = pathlib.Path(sys.argv[2])
receipt = pathlib.Path(sys.argv[3])
receiving_sha = sys.argv[4]
receiving_tree = sys.argv[5]
plan_sha = sys.argv[6]
subprocess.run(
    [
        str(root / "tools/mem_leak_checker_task5_scenarios.sh"),
        "validate-receiving",
        "--receipt",
        str(receipt.relative_to(root)),
        "--task",
        "5",
    ],
    cwd=root,
    check=True,
    capture_output=True,
)
paths = [
    "os/kernel/irq/irq_csection.c",
    "os/kernel/debug/mem_leak_checker.c",
    "os/kernel/debug/mem_leak_checker_lifecycle.c",
    "os/kernel/debug/mem_leak_checker_lifecycle.h",
    "tools/mem_leak_checker_task5_model.c",
    "tools/mem_leak_checker_task5_lifecycle_test.c",
    "tools/mem_leak_checker_task5_irq_actual_test.c",
    "tools/mem_leak_checker_task5_irq_fallback_test.c",
    "tools/mem_leak_checker_task5_qa.sh",
    "tools/mem_leak_checker_task5_scenarios.sh",
    "tools/mem_leak_checker_task5_seal_test.sh",
    "tools/mem_leak_checker_task5_seal_allowlist.json",
    "tools/mem_leak_checker_qa.sh",
    "tools/mem_leak_checker_task5_stubs/arch/irq.h",
    "tools/mem_leak_checker_scenarios/task-5.json",
]
content = hashlib.sha256()
for relative in paths:
    blob = subprocess.run(
        ["/usr/bin/git", "show", f"{receiving_sha}:{relative}"], cwd=root,
        capture_output=True, check=True,
    ).stdout
    content.update(relative.encode() + b"\0" + blob)
scenario_blob = subprocess.run(
    ["/usr/bin/git", "show", f"{receiving_sha}:tools/mem_leak_checker_scenarios/task-5.json"],
    cwd=root, capture_output=True, check=True,
).stdout
scenario = json.loads(scenario_blob)
scenario_sha = hashlib.sha256(scenario_blob).hexdigest()
external_state_exclusions = [{
    "path": ".codegraph",
    "reason": "live_external_codegraph_mcp",
    "authenticated": False,
}]
content.update(
    b"external_state_exclusions\0" +
    json.dumps(external_state_exclusions, sort_keys=True, separators=(",", ":")).encode()
)
publication = {
    "atomic_visibility": False,
    "concurrency_scope": "cooperating_sealers_only",
    "cryptographic_authentication": False,
    "directory_fsync": True,
    "external_writer_authenticated": False,
    "external_writer_mutation_scope": "detected_where_observed_otherwise_outside_receipt_trust_boundary",
    "file_fsync": True,
    "idempotent_replay_fsync": True,
    "immutable": False,
    "mac_authenticated": False,
    "mode": "exclusive_final_inode_weaker_exfat",
    "reopened_named_payload_fsync": True,
    "surviving_identity_validation": "original_descriptor_or_reopened_exact_named_payload",
}
content.update(
    b"publication\0" + json.dumps(publication, sort_keys=True, separators=(",", ":")).encode()
)
content.update(b"normalized_plan_sha256\0" + plan_sha.encode())
value = {
    "schema": 2,
    "task": 5,
    "status": "host_scenarios_sealed_qemu_explicitly_deferred",
    "receiving_sha": receiving_sha,
    "receiving_tree": receiving_tree,
    "content_sha256": content.hexdigest(),
    "scenario_sha256": scenario_sha,
    "normalized_plan_sha256": plan_sha,
    "scenario_exits": {item["kind"]: item["expected_exit"] for item in scenario["scenarios"]},
    "scenario_commands": {item["kind"]: item["command"] for item in scenario["scenarios"]},
    "qemu": scenario["qemu"],
    "external_state_exclusions": external_state_exclusions,
    "publication": publication,
}
payload = json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n"
encoded = payload.encode()

def require_identical(descriptor):
    if not stat.S_ISREG(os.fstat(descriptor).st_mode):
        raise SystemExit("Task5 receipt winner is not regular")
    chunks = []
    while chunk := os.read(descriptor, 65536):
        chunks.append(chunk)
    if b"".join(chunks) != encoded:
        raise SystemExit("Task5 idempotent seal receipt drift")

def sync_identical_survivor(directory, descriptor):
    require_identical(descriptor)
    if os.environ.get("MLC_TASK5_IDENTICAL_FSYNC_FAILURE") == "1":
        if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
            raise SystemExit("Task5 identical-fsync fault escaped its private fixture")
        raise OSError("injected Task5 identical receipt fsync failure")
    os.fsync(descriptor)
    os.fsync(directory)
    marker_value = os.environ.get("MLC_TASK5_SURVIVOR_FSYNC_MARKER")
    if marker_value is not None:
        marker = pathlib.Path(marker_value).resolve(strict=False)
        if marker.parent != root.parent or not str(root).startswith(
            ("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")
        ):
            raise SystemExit("Task5 survivor marker escaped its private fixture")
        marker.write_text("reopened_named_file_and_directory_fsynced\n")

def cleanup_created_receipt(directory, descriptor, owned, owned_name_identity):
    identity = (owned.st_dev, owned.st_ino) if owned is not None else None
    if identity is None:
        try:
            if os.environ.get("MLC_TASK5_CLEANUP_FSTAT_FAILURE") == "1":
                if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                    raise SystemExit("Task5 cleanup-fstat fault escaped its private fixture")
                raise OSError("injected Task5 cleanup fstat failure")
            descriptor_identity = os.fstat(descriptor)
            identity = (descriptor_identity.st_dev, descriptor_identity.st_ino)
        except OSError:
            identity = owned_name_identity
    if identity is None:
        return
    try:
        named_receipt = os.stat(receipt.name, dir_fd=directory, follow_symlinks=False)
    except FileNotFoundError:
        return
    if (named_receipt.st_dev, named_receipt.st_ino) != identity:
        return
    os.unlink(receipt.name, dir_fd=directory)
    os.fsync(directory)
    marker_value = os.environ.get("MLC_TASK5_CLEANUP_MARKER")
    if marker_value is not None:
        marker = pathlib.Path(marker_value).resolve(strict=False)
        if marker.parent != root.parent or not str(root).startswith(
            ("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")
        ):
            raise SystemExit("Task5 cleanup marker escaped its private fixture")
        marker.write_text("owned_receipt_unlinked_directory_fsynced\n")

if os.environ.get("MLC_TASK5_FINAL_WINDOW_MUTATION") == "1":
    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
        raise SystemExit("Task5 final-window mutation escaped its private fixture")
    with (root / "os/kernel/irq/irq_csection.c").open("ab") as stream:
        stream.write(b"final-window mutation\n")
relative_receipt = receipt.relative_to(root)
lock_fd = int(os.environ["MLC_TASK5_LOCK_FD"])
directory = os.dup(lock_fd)
locked = os.fstat(directory)
named = os.stat(receipt.parent, follow_symlinks=False)
if not stat.S_ISDIR(locked.st_mode) or (locked.st_dev, locked.st_ino) != (named.st_dev, named.st_ino):
    raise SystemExit("Task5 lifecycle lock directory identity mismatch")
try:
    final_validation = subprocess.run(
        [
            str(root / "tools/mem_leak_checker_task5_scenarios.sh"),
            "validate-receiving",
            "--receipt",
            str(receipt.relative_to(root)),
            "--task",
            "5",
        ],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    if final_validation.stdout.strip() != plan_sha:
        raise SystemExit("Task5 authenticated plan changed before publication")
    try:
        descriptor = os.open(
            receipt.name,
            os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0),
            dir_fd=directory,
        )
    except FileNotFoundError:
        try:
            descriptor = os.open(
                receipt.name,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
                0o600,
                dir_fd=directory,
            )
        except FileExistsError:
            descriptor = os.open(
                receipt.name,
                os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0),
                dir_fd=directory,
            )
            try:
                sync_identical_survivor(directory, descriptor)
            finally:
                os.close(descriptor)
            descriptor = None
        if descriptor is None:
            pass
        else:
            owned = None
            owned_name_identity = None
            try:
                if os.environ.get("MLC_TASK5_NAME_STAT_FAILURE") == "1":
                    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                        raise SystemExit("Task5 named-stat fault escaped its private fixture")
                    raise OSError("injected Task5 named-stat failure")
                created_name = os.stat(receipt.name, dir_fd=directory, follow_symlinks=False)
                if not stat.S_ISREG(created_name.st_mode):
                    raise SystemExit("Task5 created receipt is not regular")
                owned_name_identity = (created_name.st_dev, created_name.st_ino)
                if os.environ.get("MLC_TASK5_PRE_FSTAT_FOREIGN_SUBSTITUTION") == "1":
                    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                        raise SystemExit("Task5 foreign substitution escaped its private fixture")
                    os.unlink(receipt.name, dir_fd=directory)
                    foreign = os.open(
                        receipt.name,
                        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
                        0o600,
                        dir_fd=directory,
                    )
                    try:
                        os.write(foreign, b"foreign Task5 receipt\n")
                        os.fsync(foreign)
                    finally:
                        os.close(foreign)
                    os.fsync(directory)
                    raise OSError("injected Task5 foreign substitution")
                if os.environ.get("MLC_TASK5_PRE_FSTAT_FAILURE") == "1":
                    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                        raise SystemExit("Task5 pre-fstat fault escaped its private fixture")
                    raise OSError("injected Task5 pre-fstat failure")
                owned = os.fstat(descriptor)
                written = 0
                while written < len(encoded):
                    count = os.write(descriptor, encoded[written:])
                    if count <= 0:
                        raise OSError("short Task5 receipt write")
                    written += count
                os.fsync(descriptor)
                if os.environ.get("MLC_TASK5_IDENTICAL_SUBSTITUTION") == "1":
                    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                        raise SystemExit("Task5 identical substitution escaped its private fixture")
                    os.unlink(receipt.name, dir_fd=directory)
                    survivor = os.open(
                        receipt.name,
                        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
                        0o600,
                        dir_fd=directory,
                    )
                    try:
                        written = 0
                        while written < len(encoded):
                            count = os.write(survivor, encoded[written:])
                            if count <= 0:
                                raise OSError("short Task5 survivor write")
                            written += count
                    finally:
                        os.close(survivor)
                if os.environ.get("MLC_TASK5_AFTER_WRITE_MUTATION") == "1":
                    if not str(root).startswith(("/tmp/mlc-task5-seal-test.", "/private/tmp/mlc-task5-seal-test.")):
                        raise SystemExit("Task5 write mutation escaped its private fixture")
                    with (root / "os/kernel/irq/irq_csection.c").open("ab") as stream:
                        stream.write(b"after-write mutation\n")
                subprocess.run(
                    [
                        str(root / "tools/mem_leak_checker_task5_scenarios.sh"),
                        "validate-receiving",
                        "--receipt",
                        str(receipt.relative_to(root)),
                        "--task",
                        "5",
                    ],
                    cwd=root,
                    check=True,
                    capture_output=True,
                )
                named_receipt = os.stat(receipt.name, dir_fd=directory, follow_symlinks=False)
                if (named_receipt.st_dev, named_receipt.st_ino) != (owned.st_dev, owned.st_ino):
                    survivor = os.open(
                        receipt.name,
                        os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0),
                        dir_fd=directory,
                    )
                    try:
                        sync_identical_survivor(directory, survivor)
                    finally:
                        os.close(survivor)
                else:
                    os.fsync(directory)
            except BaseException as error:
                try:
                    cleanup_created_receipt(
                        directory,
                        descriptor,
                        owned,
                        owned_name_identity,
                    )
                except BaseException as cleanup_error:
                    error.add_note(f"Task5 receipt cleanup also failed: {cleanup_error}")
                raise
            finally:
                os.close(descriptor)
    else:
        try:
            sync_identical_survivor(directory, descriptor)
        finally:
            os.close(descriptor)
finally:
    os.close(directory)
print(content.hexdigest(), scenario_sha)
PY
		)
		content_sha=${seal_values%% *}
		scenario_sha=${seal_values#* }
		validate_receipt_file "$evidence/task-5-post-integration-$receiving_sha.json" \
			"$receiving_sha" "$receiving_tree" "$content_sha" "$scenario_sha" "$plan_sha"
		final_plan_sha=$(validate_seal_worktree "$repo_root" "$receipt_relative" 5)
		[ "$final_plan_sha" = "$plan_sha" ] || fail "Task5 receiving context changed at publication"
		printf 'MLC_QA_SEAL task=5 status=host_scenarios_sealed_qemu_explicitly_deferred receipt=%s\n' \
			"$evidence/task-5-post-integration-$receiving_sha.json"
		;;
	*) fail "unknown Task5 scenario command: $command" ;;
esac
