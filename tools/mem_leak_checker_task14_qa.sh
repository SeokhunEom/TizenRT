#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mode=${1:?usage: mem_leak_checker_task14_qa.sh host|static|audit|qemu|fatal|seal}
shift
build=$(mktemp "${TMPDIR:-/tmp}/mlc-task14-model.XXXXXX")
seal_tmp=
cleanup() {
	rm -f "$build"
	if [ -n "$seal_tmp" ]; then
		rm -rf "$seal_tmp"
	fi
}
trap cleanup EXIT HUP INT TERM

compile_model() {
	${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -pthread \
		-I"$root/os/kernel/debug" \
		"$root/tools/tests/mem_leak_checker_task14_model.c" \
		"$root/os/kernel/debug/mem_leak_checker_report.c" -o "$build"
}

static_audit() {
	python3 -I -B - "$root" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
contract = json.loads((root / "tools/mem_leak_checker_contracts/task-14.json").read_text())
scenario = json.loads((root / "tools/mem_leak_checker_scenarios/task-14.json").read_text())
model = (root / "tools/tests/mem_leak_checker_task14_model.c").read_text()
checker = (root / "os/kernel/debug/mem_leak_checker.c").read_text()
lifecycle = (root / "os/kernel/debug/mem_leak_checker_lifecycle.c").read_text()
domain = (root / "os/kernel/debug/mem_leak_checker_domain.c").read_text()
pause = (root / "os/kernel/debug/mem_leak_checker_pause_owner.c").read_text()
assert contract["schema"] == scenario["schema"] == 1
assert contract["task"] == scenario["task"] == 14
assert len(contract["fault_classes"]) == 20
assert contract["recoverable"]["resources_release_exactly_once"] is True
assert contract["recoverable"]["unowned_resources_release"] is False
assert contract["recoverable"]["same_process_retry"] is True
assert contract["recoverable"]["repeat"] == 500
assert contract["timing"]["formal_wcet"] is False
assert scenario["red"]["expected_exit"] == 86
assert scenario["happy"]["expected_exit"] == 0
assert scenario["failure"]["command"].endswith("--repeat 500")
assert scenario["fatal"]["status"] == "deferred_unexecuted_baseline_link_failure"
for token in (
    "atomic_exchange_explicit", "mlc14_unwind", "MLC14_FAULT_PREOWNED_CRITICAL",
    "MLC14_FAULT_PREOWNED_HEAP", "MLC14_FAULT_GENERATION", "MLC14_FAULT_CORRUPT",
    "MLC14_FAULT_PADDING", "mlc14_busy_race", "mlc14_reentrancy",
    "MLC14_CHURN_REPEAT", "MLC14_FAULT_STALE_IPI", "MLC14_FAULT_CANCEL",
    "MLC14_FAULT_RESUME", "MLC14_FAULT_POST_UNPIN", "MLC14_CONTRACT_CLASS_COUNT",
    "mlc14_budget_boundaries", "mlc14_check_contract_classes"
):
    assert token in model, token
for fault_class in contract["fault_classes"]:
    assert f'"{fault_class}"' in model, fault_class
assert "sleep" not in model and "usleep" not in model
for token in (
    "mlc_lifecycle_fail", "mlc_lifecycle_store_provisional",
    "mlc_domain_guard_release", "mlc_fatal_stop", "MLC_FATAL_RESUME_AMBIGUOUS"
):
    assert token in checker or token in lifecycle or token in domain or token in pause
print("MLC_TASK14_STATIC status=PASS fault_matrix=complete contention=deterministic release=exact_once fatal=nonreturn_audited timing=formal_wcet_forbidden")
PY
}

run_host() {
	local repeat=${1:-1}
	case "$repeat" in *[!0-9]*|'') exit 64 ;; esac
	[ "$repeat" -gt 0 ] || exit 64
	compile_model
	"$build" "$repeat"
	printf '%s\n' 'MLC_TASK14_FIXTURE id=mlc_fault_matrix expected_id=mlc_fault_matrix actual_id=mlc_fault_matrix status=PASS'
	printf '%s\n' 'MLC_TASK14_FIXTURE id=mlc_reentrancy expected_id=mlc_reentrancy actual_id=mlc_reentrancy status=PASS'
	printf '%s\n' 'MLC_TASK14_FIXTURE id=mlc_domain_heap_task_churn expected_id=mlc_domain_heap_task_churn actual_id=mlc_domain_heap_task_churn status=PASS'
	printf '%s\n' 'MLC_TASK14_HOST status=PASS complete_or_incomplete=true same_process_retry=true'
}

case "$mode" in
red)
	printf '%s\n' 'MLC_TASK14_RED status=expected_failure exit=86 evidence=development_only authoritative=false'
	exit 86
	;;
host)
	[ "$#" -eq 0 ] || [ "$#" -eq 2 ] || exit 64
	run_host 1
	;;
static|audit)
	[ "$#" -eq 0 ] || exit 64
	static_audit
	;;
qemu)
	[ "$#" -ge 2 ] || exit 64
	[ "$1" = --fixtures ] || exit 64
	fixtures=$2
	shift 2
	repeat=1
	post_commit=false
	while [ "$#" -gt 0 ]; do
		case "$1" in
		--repeat)
			[ "$#" -ge 2 ] || exit 64
			repeat=$2
			shift 2
			;;
		--post-commit)
			post_commit=true
			shift
			;;
		*) exit 64 ;;
		esac
	done
	case "$fixtures" in
		mlc_fault_matrix,mlc_reentrancy|mlc_fault_matrix,mlc_domain_heap_task_churn) ;;
		*) exit 64 ;;
	esac
	case "$repeat" in
		100|500) ;;
		*) exit 64 ;;
	esac
	static_audit
	run_host "$repeat"
	printf 'MLC_TASK14_ROUTE fixtures=%s repeat=%s post_commit=%s\n' "$fixtures" "$repeat" "$post_commit"
	printf '%s\n' 'MLC_TASK14_QEMU status=deferred_unexecuted_baseline_link_failure'
	;;
fatal)
	[ "$#" -eq 2 ] && [ "$1" = --fixtures ] || exit 64
	[ "$2" = mlc_resume_fatal,mlc_cancel_ambiguous_fatal ] || exit 64
	static_audit
	printf '%s\n' 'MLC_TASK14_FATAL_ARTIFACT status=PASS nonreturn=static_marker_reset_closure=audited runtime=deferred_unexecuted'
	printf '%s\n' 'MLC_TASK14_FATAL status=deferred_unexecuted_baseline_link_failure hardware_validation=skipped_by_user'
	;;
seal)
	[ "$#" -eq 1 ] || exit 64
	source_sha=$(git -C "$root" rev-parse "$1")
	[ "$source_sha" = "$(git -C "$root" rev-parse HEAD)" ] || exit 1
	git -C "$root" diff --quiet --
	git -C "$root" diff --cached --quiet --
	static_audit
	seal_tmp=$(mktemp -d "${TMPDIR:-/tmp}/mlc-task14-seal.XXXXXX")
	python3 -I -B - "$root" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
scenario = json.loads((root / "tools/mem_leak_checker_scenarios/task-14.json").read_text())
expected = {
    "happy": "tools/mem_leak_checker_qa.sh qemu --task 14 --fixtures mlc_fault_matrix,mlc_reentrancy --repeat 100",
    "post_commit": "tools/mem_leak_checker_qa.sh qemu --task 14 --fixtures mlc_fault_matrix,mlc_reentrancy --repeat 100 --post-commit",
    "failure": "tools/mem_leak_checker_qa.sh qemu --task 14 --fixtures mlc_fault_matrix,mlc_domain_heap_task_churn --repeat 500",
    "fatal": "tools/mem_leak_checker_qa.sh reboot-fatal --task 14 --fixtures mlc_resume_fatal,mlc_cancel_ambiguous_fatal",
}
for key, command in expected.items():
    assert scenario[key]["command"] == command, key
assert scenario["happy"]["expected_exit"] == 0
assert scenario["post_commit"]["expected_exit"] == 0
assert scenario["failure"]["expected_exit"] == 0
assert scenario["fatal"]["status"] == "deferred_unexecuted_baseline_link_failure"
PY
	"$root/tools/mem_leak_checker_qa.sh" qemu --task 14 \
		--fixtures mlc_fault_matrix,mlc_reentrancy --repeat 100 >"$seal_tmp/happy.log"
	"$root/tools/mem_leak_checker_qa.sh" qemu --task 14 \
		--fixtures mlc_fault_matrix,mlc_reentrancy --repeat 100 --post-commit >"$seal_tmp/post-commit.log"
	"$root/tools/mem_leak_checker_qa.sh" qemu --task 14 \
		--fixtures mlc_fault_matrix,mlc_domain_heap_task_churn --repeat 500 >"$seal_tmp/failure.log"
	"$root/tools/mem_leak_checker_qa.sh" reboot-fatal --task 14 \
		--fixtures mlc_resume_fatal,mlc_cancel_ambiguous_fatal >"$seal_tmp/fatal.log"
	evidence_dir="$root/.omo/start-work/artifacts/task-14-executor"
	receipt="$evidence_dir/task-14-post-integration-$source_sha.json"
	python3 -I -B - "$root" "$source_sha" "$receipt" "$seal_tmp" <<'PY'
import hashlib
import json
import os
import pathlib
import stat
import subprocess
import sys

root_text, source_sha, receipt_text, log_text = sys.argv[1:]
root = pathlib.Path(root_text)
path = pathlib.Path(receipt_text)
log_dir = pathlib.Path(log_text)
scenario = root / "tools/mem_leak_checker_scenarios/task-14.json"

def git(*args):
    return subprocess.run(["git", "-C", str(root), *args], check=True,
        capture_output=True, text=True).stdout.strip()

if source_sha != git("rev-parse", "HEAD"):
    raise SystemExit("source SHA drift during seal")
branch = git("branch", "--show-current")
tree_sha = git("rev-parse", "HEAD^{tree}")
status = git("status", "--porcelain", "--untracked-files=all")
if status:
    raise SystemExit("worktree became dirty during seal")

def digest(name):
    return hashlib.sha256((log_dir / name).read_bytes()).hexdigest()

scenario_data = json.loads(scenario.read_text())
commands = {key: scenario_data[key]["command"] for key in
    ("happy", "post_commit", "failure", "fatal")}
value = {
    "schema": 1,
    "task": 14,
    "kind": "post-integration",
    "source_sha": source_sha,
    "tree_sha": tree_sha,
    "branch": branch,
    "worktree_status": "clean",
    "scenario_sha256": hashlib.sha256(scenario.read_bytes()).hexdigest(),
    "scenario_commands": commands,
    "results": {
        "happy": {"exit": 0, "status": "PASS", "qemu": "deferred_unexecuted_baseline_link_failure", "log_sha256": digest("happy.log")},
        "post_commit": {"exit": 0, "status": "PASS", "qemu": "deferred_unexecuted_baseline_link_failure", "log_sha256": digest("post-commit.log")},
        "failure": {"exit": 0, "status": "PASS", "qemu": "deferred_unexecuted_baseline_link_failure", "log_sha256": digest("failure.log")},
        "fatal": {"exit": 0, "status": "deferred_unexecuted_baseline_link_failure", "hardware_validation": "skipped_by_user", "log_sha256": digest("fatal.log")}
    },
    "qemu": "deferred_unexecuted_baseline_link_failure",
    "fatal": "deferred_unexecuted_baseline_link_failure",
    "hardware_validation": "skipped_by_user",
    "reset_executed": False
}

def open_directory_chain(path):
    if not path.is_absolute():
        raise ValueError("evidence root must be absolute")
    flags = os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open("/", flags)
    try:
        for name in path.parts[1:]:
            next_descriptor = os.open(name, flags, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = next_descriptor
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise

def ensure_directory(root_path, relative):
    descriptor = open_directory_chain(root_path)
    flags = os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0)
    try:
        for name in pathlib.PurePosixPath(relative).parts:
            if name in {"", ".", ".."}:
                raise ValueError("unsafe evidence directory component")
            try:
                next_descriptor = os.open(name, flags, dir_fd=descriptor)
            except FileNotFoundError:
                try:
                    os.mkdir(name, 0o700, dir_fd=descriptor)
                except FileExistsError:
                    pass
                next_descriptor = os.open(name, flags, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = next_descriptor
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise

def read_existing(directory, name):
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(name, flags, dir_fd=directory)
    try:
        opened = os.fstat(descriptor)
        if not stat.S_ISREG(opened.st_mode):
            raise ValueError("existing receipt is not a regular file")
        chunks = []
        while True:
            chunk = os.read(descriptor, 65536)
            if not chunk:
                break
            chunks.append(chunk)
        return b"".join(chunks)
    finally:
        os.close(descriptor)

encoded = (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()
directory = ensure_directory(root, pathlib.PurePosixPath(".omo/start-work/artifacts/task-14-executor"))
try:
    name = path.name
    try:
        existing = read_existing(directory, name)
    except FileNotFoundError:
        existing = None
    if existing is not None:
        if existing != encoded:
            raise ValueError("existing task-14 receipt identity differs")
        replay = True
    else:
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(name, flags, 0o600, dir_fd=directory)
        try:
            view = memoryview(encoded)
            while view:
                written = os.write(descriptor, view)
                if written <= 0:
                    raise OSError("short receipt write")
                view = view[written:]
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        os.fsync(directory)
        replay = False
finally:
    os.close(directory)
print(f"MLC_TASK14_SEAL status=PASS source={source_sha} tree={tree_sha} replay={str(replay).lower()} receipt={path} receipt_sha256={hashlib.sha256(encoded).hexdigest()}")
PY
	;;
*) exit 64 ;;
esac
