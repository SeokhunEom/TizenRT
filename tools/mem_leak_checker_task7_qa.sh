#!/bin/bash -p
set -euo pipefail

trusted_path=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
PATH=$trusted_path
export PATH PYTHONDONTWRITEBYTECODE=1

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
source_root=$(CDPATH= cd -- "$script_dir/.." && pwd -P)

fail() {
	printf '%s\n' "mem-leak-checker Task7 QA failed: $*" >&2
	exit 1
}

run_host() {
	selector=$1
	fixtures=$2
	"$script_dir/test_mem_leak_checker_candidates.sh" "--$selector" "$fixtures"
}

case "${1:-}" in
	red)
		[ "$#" -eq 1 ] || exit 64
		exec env MLC_TASK7_EXPECT_RED=1 \
			"$script_dir/test_mem_leak_checker_candidates.sh" \
			--fixture mlc_candidate_manifest
		;;
	qemu)
		[ "$#" -eq 7 ] || [ "$#" -eq 8 ] || exit 64
		[ "$2" = --selector ] && [ "$4" = --fixtures ] &&
			[ "$6" = --post-commit ] || exit 64
		selector=$3
		fixtures=$5
		post_commit=$7
		[ "$selector" = fixture ] || [ "$selector" = fixtures ] || exit 64
		[ "$post_commit" = true ] || [ "$post_commit" = false ] || exit 64
		case "$selector:$fixtures:$post_commit" in
			fixtures:mlc_candidate_manifest,mlc_control_source_target_exclusions,mlc_heap_backed_loadable_root:true|\
			fixtures:mlc_candidate_corruption_and_lock_contention,mlc_tcb_stack_root_policy,mlc_invalid_loadable_root_container:false) ;;
			*) fail "noncanonical fixture selection" ;;
		esac
		run_host "$selector" "$fixtures"
		printf '%s\n' 'MLC_TASK7_QEMU status=deferred_unexecuted_baseline_link_failure'
		;;
	seal)
		[ "$#" -eq 2 ] || exit 64
		receiving_sha=$(git -C "$source_root" rev-parse "$2")
		[ "$receiving_sha" = "$(git -C "$source_root" rev-parse HEAD)" ] ||
			fail "receiving SHA drift"
		[ -z "$(git -C "$source_root" status --porcelain --untracked-files=all)" ] ||
			fail "seal requires clean worktree"
		receiving_tree=$(git -C "$source_root" rev-parse HEAD^{tree})
		patch_sha=$(git -C "$source_root" diff --binary \
			c93078ab05bb6463467669fb6ee19bb75ee7eaba.."$receiving_sha" |
			shasum -a 256 | awk '{print $1}')
		plan_sha=$(shasum -a 256 \
			"$source_root/.omo/plans/mem-leak-checker-hardening.md" | awk '{print $1}')
		source_manifest_sha=$(git -C "$source_root" diff --name-only \
			c93078ab05bb6463467669fb6ee19bb75ee7eaba.."$receiving_sha" |
			while IFS= read -r path; do
				git -C "$source_root" rev-parse "$receiving_sha:$path" || exit 1
			done | shasum -a 256 | awk '{print $1}')
		scenario=$script_dir/mem_leak_checker_scenarios/task-7.json
		scenario_sha=$(shasum -a 256 "$scenario" | awk '{print $1}')
		happy_command='tools/mem_leak_checker_qa.sh qemu --task 7 --fixtures mlc_candidate_manifest,mlc_control_source_target_exclusions,mlc_heap_backed_loadable_root --post-commit'
		failure_command='tools/mem_leak_checker_qa.sh qemu --task 7 --fixtures mlc_candidate_corruption_and_lock_contention,mlc_tcb_stack_root_policy,mlc_invalid_loadable_root_container'
		happy_output=$($source_root/$happy_command)
		failure_output=$($source_root/$failure_command)
		expected_happy=$'MLC_TASK7_FIXTURE name=mlc_candidate_manifest status=PASS\nMLC_TASK7_FIXTURE name=mlc_control_source_target_exclusions status=PASS\nMLC_TASK7_FIXTURE name=mlc_heap_backed_loadable_root status=PASS\nMLC_TASK7_QEMU status=deferred_unexecuted_baseline_link_failure'
		expected_failure=$'MLC_TASK7_FIXTURE name=mlc_candidate_corruption_and_lock_contention status=PASS\nMLC_TASK7_FIXTURE name=mlc_tcb_stack_root_policy status=PASS\nMLC_TASK7_FIXTURE name=mlc_invalid_loadable_root_container status=PASS\nMLC_TASK7_QEMU status=deferred_unexecuted_baseline_link_failure'
		[ "$happy_output" = "$expected_happy" ] || fail "happy transcript mismatch"
		[ "$failure_output" = "$expected_failure" ] || fail "failure transcript mismatch"
		happy_sha=$(printf '%s\n' "$happy_output" | shasum -a 256 | awk '{print $1}')
		failure_sha=$(printf '%s\n' "$failure_output" | shasum -a 256 | awk '{print $1}')
		[ -f "$source_root/.omo/boulder.json" ] || fail "authoritative Boulder missing"
		[ -f "$source_root/.omo/start-work/ledger.jsonl" ] || fail "authoritative ledger missing"
		[ "$(git -C "$source_root" branch --show-current)" = \
			codex/mem-leak-checker-hardening ] || fail "authoritative branch mismatch"
			python3 - "$source_root" <<'PY'
import json
import os
import pathlib
import stat
import sys
import tempfile

root = pathlib.Path(sys.argv[1]).resolve()
document = json.loads((root / ".omo/boulder.json").read_text(encoding="utf-8"))
work_id = document.get("active_work_id")
work = document.get("works", {}).get(work_id, {})
if document.get("schema_version") != 2 or work_id != "mem-leak-checker-hardening-257754dc":
    raise SystemExit("Boulder schema mismatch")
if work.get("active_plan") != ".omo/plans/mem-leak-checker-hardening.md":
    raise SystemExit("Boulder plan mismatch")
if pathlib.Path(work.get("worktree_path", "")).resolve() != root:
    raise SystemExit("Boulder worktree mismatch")
if work.get("status") != "active":
    raise SystemExit("Boulder status mismatch")
if work.get("session_ids") != ["codex:257754dc-1fe6-461f-86d4-57a7d2b9fb8d"]:
    raise SystemExit("Boulder session mismatch")
ledger = root / ".omo/start-work/ledger.jsonl"
events = [json.loads(line) for line in ledger.read_text(encoding="utf-8").splitlines()]
if not events or events[0].get("baseline_sha") != "c93078ab05bb6463467669fb6ee19bb75ee7eaba":
    raise SystemExit("ledger baseline mismatch")
if any(event.get("plan") != ".omo/plans/mem-leak-checker-hardening.md" or
       event.get("session_id") != "codex:257754dc-1fe6-461f-86d4-57a7d2b9fb8d"
       for event in events):
    raise SystemExit("ledger identity mismatch")
PY
		output_dir=$source_root/.omo/start-work/artifacts/task-7-executor
		mkdir -p "$output_dir"
		authoritative=true
		receipt=$output_dir/task-7-post-integration-$receiving_sha.json
		python3 - "$receipt" "$receiving_sha" "$receiving_tree" \
			"$scenario_sha" "$happy_sha" "$failure_sha" "$authoritative" \
			"$happy_command" "$failure_command" "$patch_sha" "$plan_sha" \
			"$source_manifest_sha" <<'PY'
import json
import os
import pathlib
import stat
import sys

path_text, sha, tree, scenario, happy, failure, authoritative, happy_command, failure_command, patch, plan, source_manifest = sys.argv[1:]
path = pathlib.Path(path_text)
if not hasattr(os, "O_NOFOLLOW"):
    raise SystemExit("O_NOFOLLOW unsupported")
document = {
    "authoritative": authoritative == "true",
    "commands": [
        {"command": happy_command, "exit": 0, "kind": "happy", "stdout_sha256": happy},
        {"command": failure_command, "exit": 0, "kind": "failure", "stdout_sha256": failure},
    ],
    "manifest_equation": "allocated_count=candidate_count+exclusion_count",
    "baseline_sha": "c93078ab05bb6463467669fb6ee19bb75ee7eaba",
    "patch_sha256": patch,
    "plan_sha256": plan,
    "source_manifest_sha256": source_manifest,
    "qemu": "deferred_unexecuted_baseline_link_failure",
    "receiving_sha": sha,
    "receiving_tree": tree,
    "runtime_claim": False,
    "scenario_sha256": scenario,
    "schema": 2,
    "task": 7,
}
payload = (json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n").encode()
path.parent.mkdir(parents=True, exist_ok=True)
flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
try:
    descriptor = os.open(path, flags, 0o600)
except FileExistsError:
    read_fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    try:
        if not stat.S_ISREG(os.fstat(read_fd).st_mode):
            raise SystemExit("existing Task7 receipt is not regular")
        existing = b""
        while True:
            chunk = os.read(read_fd, 65536)
            if not chunk:
                break
            existing += chunk
    finally:
        os.close(read_fd)
    if existing != payload:
        raise SystemExit("existing Task7 receipt differs")
else:
    offset = 0
    while offset < len(payload):
        written = os.write(descriptor, payload[offset:])
        if written <= 0:
            raise SystemExit("Task7 receipt short write")
        offset += written
    os.fsync(descriptor)
    os.close(descriptor)
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)
print(path)
PY
		"$script_dir/mem_leak_checker_task7_receipt.py" validate "$receipt" < "$receipt"
		printf '%s\n' "$happy_output" "$failure_output"
		printf 'MLC_TASK7_SEAL status=PASS receiving_sha=%s authoritative=%s receipt=%s\n' \
			"$receiving_sha" "$authoritative" "$receipt"
		;;
	negative)
		[ "$#" -eq 1 ] || exit 64
		python3 - "$script_dir/mem_leak_checker_scenarios/task-7.json" \
			"$script_dir/mem_leak_checker_task7_receipt.py" <<'PY'
import copy
import json
import os
import pathlib
import subprocess
import sys
import tempfile

document = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
publisher = pathlib.Path(sys.argv[2])
if document.get("task") != 7 or document.get("schema") != 1:
    raise SystemExit(1)
if document.get("qemu") != "deferred_unexecuted_baseline_link_failure":
    raise SystemExit(1)
if len(document.get("happy", ())) != 1 or len(document.get("failure", ())) != 1:
    raise SystemExit(1)
commands = [entry["command"] for entry in
            (document["happy"] + document["failure"])]
receipt = {
    "authoritative": True,
    "baseline_sha": "b" * 64,
    "commands": [
        {"command": commands[0], "exit": 0, "kind": "happy",
         "stdout_sha256": "1" * 64},
        {"command": commands[1], "exit": 0, "kind": "failure",
         "stdout_sha256": "2" * 64},
    ],
    "manifest_equation": "allocated_count=candidate_count+exclusion_count",
    "patch_sha256": "c" * 64,
    "plan_sha256": "d" * 64,
    "qemu": document["qemu"],
    "receiving_sha": "e" * 64,
    "receiving_tree": "f" * 64,
    "runtime_claim": False,
    "scenario_sha256": "0" * 64,
    "schema": 2,
    "source_manifest_sha256": "a" * 64,
    "task": 7,
}
payload = (json.dumps(receipt, sort_keys=True, separators=(",", ":")) +
           "\n").encode()


def publish(path, content):
    subprocess.run([sys.executable, str(publisher), "publish", str(path)],
                   input=content, check=True)


def reject_validation(path, expected):
    result = subprocess.run(
        [sys.executable, str(publisher), "validate", str(path)],
        input=expected)
    if result.returncode == 0:
        raise SystemExit(f"mutation accepted: {path.name}")


with tempfile.TemporaryDirectory(prefix="mlc-task7-receipt-negative-") as temp:
    root = pathlib.Path(temp)
    path = root / "receipt"
    publish(path, payload)
    subprocess.run([sys.executable, str(publisher), "validate", str(path)],
                   input=payload, check=True)
    for mutation in ("omission", "surplus", "content", "tree", "plan",
                     "command", "exit"):
        changed = copy.deepcopy(receipt)
        if mutation == "omission":
            changed.pop("plan_sha256")
        elif mutation == "surplus":
            changed["unexpected"] = True
        elif mutation == "content":
            changed["receiving_sha"] = "1" * 64
        elif mutation == "tree":
            changed["receiving_tree"] = "1" * 64
        elif mutation == "plan":
            changed["plan_sha256"] = "1" * 64
        elif mutation == "command":
            changed["commands"][0]["command"] += " --mutated"
        else:
            changed["commands"][1]["exit"] = 1
        changed_payload = (json.dumps(changed, sort_keys=True,
                                      separators=(",", ":")) + "\n").encode()
        mutation_path = root / mutation
        publish(mutation_path, changed_payload)
        reject_validation(mutation_path, payload)
    if subprocess.run([sys.executable, str(publisher), "publish", str(path)],
                      input=payload).returncode == 0:
        raise SystemExit("publication overwrite accepted")
    symlink = root / "symlink"
    symlink.symlink_to(path)
    if subprocess.run([sys.executable, str(publisher), "validate", str(symlink)],
                      input=payload).returncode == 0:
        raise SystemExit("symlink publication accepted")
    fifo = root / "fifo"
    os.mkfifo(fifo)
    if subprocess.run([sys.executable, str(publisher), "validate", str(fifo)],
                      input=payload).returncode == 0:
        raise SystemExit("FIFO publication accepted")
    replay = copy.deepcopy(receipt)
    replay["receiving_sha"] = "9" * 64
    replay_payload = (json.dumps(replay, sort_keys=True,
                                 separators=(",", ":")) + "\n").encode()
    reject_validation(path, replay_payload)
print("MLC_TASK7_RECEIPT_NEGATIVE status=PASS cases=omission,surplus,content,tree,plan,command,exit,publication,symlink,FIFO,replay")
PY
		;;
	*) exit 64 ;;
esac
