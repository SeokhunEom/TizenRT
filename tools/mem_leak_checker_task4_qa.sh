#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
source_root=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
baseline_sha=37829f7e4b8cbb3948f1451d5c693be857c551f6
fixture_paths='os/kernel/debug/tests/test_mem_leak_checker_graph.c
os/kernel/debug/tests/test_mem_leak_checker_graph_depth.c
tools/mem_leak_checker_qa.sh
tools/mem_leak_checker_scenarios/task-4.json
tools/mem_leak_checker_task4_authoritative.py
tools/mem_leak_checker_task4_evidence.py
tools/mem_leak_checker_task4_qa.sh
tools/mem_leak_checker_task4_scenarios.py
tools/test_mem_leak_checker_graph.sh'

fail() {
	printf 'mem-leak-checker task-4 QA failed: %s\n' "$*" >&2
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

evidence_dir() {
	directory=${MLC_TASK4_EVIDENCE_DIR:-}
	[ -n "$directory" ] || fail "MLC_TASK4_EVIDENCE_DIR is required"
	case "$directory" in /*) ;; *) fail "development evidence path must be absolute" ;; esac
	[ -d "$directory" ] && [ ! -L "$directory" ] || fail "development evidence directory is invalid"
	canonical=$(CDPATH= cd -- "$directory" && pwd -P)
	[ "$canonical" = "$directory" ] || fail "development evidence path is not canonical"
	case "$canonical" in "$source_root"|"$source_root"/*) fail "development evidence must be outside the source tree" ;; esac
	printf '%s\n' "$canonical"
}

fsync_file() {
	python3 - "$1" <<'PY'
import os
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
nofollow = getattr(os, "O_NOFOLLOW", 0)
directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | nofollow)
try:
    descriptor = os.open(path.name, os.O_RDONLY | nofollow, dir_fd=directory)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    os.fsync(directory)
finally:
    os.close(directory)
PY
}

verify_receipt() {
	directory=$(evidence_dir) || return
	python3 "$script_dir/mem_leak_checker_task4_evidence.py" verify "$source_root" "$directory"
}

run_red() {
	[ "$#" -eq 6 ] || fail "unexpected RED options"
	[ "$(value_after --task "$@")" = 4 ] || fail "RED task mismatch"
	[ "$(value_after --config "$@")" = qemu/tc_1m ] || fail "RED config mismatch"
	fixtures=$(value_after --fixtures "$@")
	[ "$fixtures" = mlc_graph_core,mlc_zero_graph ] || fail "RED fixture drift"
	[ "$(git -C "$source_root" rev-parse HEAD)" = "$baseline_sha" ] || fail "RED current HEAD mismatch"
	directory=$(evidence_dir)
	proof="$directory/task-4-red-proof.log"
	[ ! -e "$proof" ] && [ ! -e "$directory/task-4-development-red.json" ] || fail "development RED evidence already exists"
	set -C +e
	"$script_dir/test_mem_leak_checker_graph.sh" --fixtures "$fixtures" >"$proof" 2>&1
	proof_exit=$?
	set +C -e
	[ "$proof_exit" -ne 0 ] || fail "RED unexpectedly passed"
	fsync_file "$proof"
	receipt_sha=$(python3 "$script_dir/mem_leak_checker_task4_evidence.py" create "$source_root" "$directory" "$proof_exit")
	printf 'MLC_QA_RED task=4 fixtures=%s status=evidence_bound_expected_failure exit=86 evidence=development_only authoritative=false receipt_sha256=%s proof_exit=%s proof_log_sha256=%s publication=exclusive_final_inode_weaker_exfat\n' \
		"$fixtures" "$receipt_sha" "$proof_exit" "$(shasum -a 256 "$proof" | awk '{print $1}')"
	exit 86
}

run_red_development() {
	directory=$(value_after --evidence-dir "$@")
	[ -d "$directory" ] && [ ! -L "$directory" ] || fail "development evidence directory is invalid"
	directory=$(CDPATH= cd -- "$directory" && pwd -P)
	[ -z "$(find "$directory" -mindepth 1 -maxdepth 1 -print -quit)" ] || fail "development evidence directory must be empty"
	setup_parent=$(mktemp -d "${TMPDIR:-/tmp}/mlc-task4-red.XXXXXX")
	fixture_root="$setup_parent/repository"
	cleanup() {
		case "$setup_parent" in "${TMPDIR:-/tmp}"/mlc-task4-red.*) chmod -R u+w "$setup_parent"; rm -rf -- "$setup_parent" ;; *) fail "unsafe RED cleanup path" ;; esac
	}
	trap cleanup EXIT HUP INT TERM
	git clone --quiet --no-hardlinks "$source_root" "$fixture_root"
	git -C "$fixture_root" checkout --quiet --detach "$baseline_sha"
	while IFS= read -r relative; do
		mkdir -p "$fixture_root/$(dirname "$relative")"
		cp "$source_root/$relative" "$fixture_root/$relative"
	done <<EOF
$fixture_paths
EOF
	git -C "$fixture_root" add -- $fixture_paths
	while IFS= read -r relative; do
		mode=$(git -C "$source_root" ls-tree HEAD -- "$relative" | awk '{print $1}')
		case "$mode" in
			100644) git -C "$fixture_root" update-index --chmod=-x "$relative" ;;
			100755) git -C "$fixture_root" update-index --chmod=+x "$relative" ;;
			*) fail "unsupported receiving HEAD fixture mode: $relative" ;;
		esac
	done <<EOF
$fixture_paths
EOF
	set +e
	MLC_TASK4_EVIDENCE_DIR="$directory" "$fixture_root/tools/mem_leak_checker_qa.sh" red --task 4 --config qemu/tc_1m --fixtures mlc_graph_core,mlc_zero_graph
	red_exit=$?
	set -e
	[ "$red_exit" -eq 86 ] || fail "development RED did not exit 86"
}

run_qemu_deferred() {
	fixtures=$(value_after --fixtures "$@")
	case "$fixtures" in
		mlc_graph_core,mlc_zero_graph)
			[ "$(value_after --repeat "$@")" = 50 ] || fail "repeat mismatch"
			"$script_dir/test_mem_leak_checker_graph.sh" --fixtures "$fixtures" --repeat 50 >/dev/null
			;;
		mlc_frontier_tarjan_exhaustion,mlc_tarjan_max_depth)
			"$script_dir/test_mem_leak_checker_graph.sh" --fixtures "$fixtures" >/dev/null
			;;
		*) fail "fixture drift" ;;
	esac
	receipt_sha=$(verify_receipt) || return
	printf 'MLC_QA_QEMU task=4 fixtures=%s status=deferred_unexecuted_baseline_link_failure evidence=development_only authoritative=false red_linkage=verified receipt_sha256=%s\n' "$fixtures" "$receipt_sha"
}

run_negative() {
	directory=$(evidence_dir) || return
	python3 "$script_dir/mem_leak_checker_task4_scenarios.py" negative "$source_root" "$directory"
}

run_seal_development() {
	[ "$(git -C "$source_root" branch --show-current)" = codex/mlc-todo4 ] || fail "development seal branch mismatch"
	[ -z "$(git -C "$source_root" status --porcelain --untracked-files=all)" ] || fail "development seal requires a clean worktree"
	directory=$(evidence_dir) || return
	seal_sha=$(python3 "$script_dir/mem_leak_checker_task4_scenarios.py" seal "$source_root" "$directory")
	printf 'MLC_QA_SEAL task=4 status=PASS evidence=development_only authoritative=false qemu=deferred_unexecuted_baseline_link_failure publication=exclusive_final_inode_weaker_exfat receipt_sha256=%s\n' "$seal_sha"
}

run_seal_authoritative() {
	[ "$#" -eq 2 ] || fail "unexpected authoritative seal options"
	[ "${1:-}" = --source ] || fail "missing authoritative source option"
	if [ -z "${MLC_TASK4_LOCK_FD:-}" ]; then
		python3 - "$source_root" "$0" "$2" <<'PY'
import fcntl
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
script = sys.argv[2]
source = sys.argv[3]
relative = pathlib.PurePosixPath(".omo/start-work/artifacts/task-4-executor")
directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
try:
    for component in relative.parts:
        child = os.open(
            component,
            os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
            dir_fd=directory,
        )
        os.close(directory)
        directory = child
    fcntl.flock(directory, fcntl.LOCK_EX)
    os.set_inheritable(directory, True)
    environment = {**os.environ, "MLC_TASK4_LOCK_FD": str(directory)}
    completed = subprocess.run(
        [script, "seal-authoritative", "--source", source],
        cwd=root,
        env=environment,
        pass_fds=(directory,),
        check=False,
    )
    raise SystemExit(completed.returncode)
finally:
    os.close(directory)
PY
		exit $?
	fi
	receiving_sha=$(git -C "$source_root" rev-parse "$2")
	[ "$receiving_sha" = "$(git -C "$source_root" rev-parse HEAD)" ] || fail "authoritative receiving SHA drift"
	receipt_relative=".omo/start-work/artifacts/task-4-executor/task-4-post-integration-$receiving_sha.json"
	temp_parent=$(mktemp -d "${TMPDIR:-/tmp}/mlc-task4-authoritative.XXXXXX")
	temp_evidence="$temp_parent/evidence"
	mkdir "$temp_evidence"
	cleanup_authoritative() {
		case "$temp_parent" in
			"${TMPDIR:-/tmp}"/mlc-task4-authoritative.*) chmod -R u+w "$temp_parent"; rm -rf -- "$temp_parent" ;;
			*) fail "unsafe authoritative cleanup path" ;;
		esac
	}
	trap cleanup_authoritative EXIT HUP INT TERM
	"$0" red-development --evidence-dir "$temp_evidence" >/dev/null
	receipt="$source_root/$receipt_relative"
	receipt_sha=$(python3 "$script_dir/mem_leak_checker_task4_authoritative.py" \
		"$source_root" "$temp_evidence" "$receipt" "$receiving_sha")
	bash "$script_dir/mem_leak_checker_task5_scenarios.sh" validate-receiving \
		--receipt "$receipt_relative" --task 4 >/dev/null
	printf 'MLC_QA_SEAL task=4 status=host_scenarios_sealed_qemu_explicitly_deferred receiving_sha=%s receipt=%s receipt_sha256=%s publication=exclusive_final_inode_weaker_exfat\n' \
		"$receiving_sha" "$receipt" "$receipt_sha"
}

command=${1:-}
[ -n "$command" ] || fail "missing command"
shift
case "$command" in
	red) run_red "$@" ;;
	red-development) run_red_development "$@" ;;
	qemu) run_qemu_deferred "$@" ;;
	negative) run_negative ;;
	seal-development) run_seal_development ;;
	seal-authoritative) run_seal_authoritative "$@" ;;
	*) fail "unknown command: $command" ;;
esac
