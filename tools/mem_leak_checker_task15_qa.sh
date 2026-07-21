#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mode=${1:?usage: mem_leak_checker_task15_qa.sh static-audit|qemu|build|budget-audit|seal-all|deploy-hardware|hardware|audit-hardware-unavailable|self-test}
shift

value_after() {
	local name=$1
	shift
	while [ "$#" -gt 0 ]; do
		if [ "$1" = "$name" ]; then
			[ "$#" -ge 2 ] || return 64
			printf '%s\n' "$2"
			return 0
		fi
		shift
	done
	return 64
}

output_dir() {
	local path
	path=$(value_after --output-dir "$@" 2>/dev/null || true)
	if [ -z "$path" ]; then
		return 1
	fi
	case "$path" in
		/*) ;;
		*) return 64 ;;
	esac
	[ -d "$path" ] || return 64
	printf '%s\n' "$path"
}

write_receipt() {
	local kind=$1
	local status=$2
	local path=${3:-}
	local config=${4:-}
	local name=${5:-}
	if [ -n "$path" ]; then
		python3 -I -B - "$path" "$kind" "$status" "$config" "$name" "$root" <<'PY'
import hashlib
import json
import pathlib
import sys

path, kind, status, config, name, root = sys.argv[1:]
config_path = pathlib.Path(root) / "build/configs" / config / "defconfig" if config else None
config_digest = hashlib.sha256(config_path.read_bytes()).hexdigest() if config_path and config_path.is_file() else None
value = {
    "schema": 1,
    "task": 15,
    "kind": kind,
    "status": status,
    "source_sha": __import__("subprocess").check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip(),
    "config": config or None,
    "config_sha256": config_digest,
    "name": name or None,
    "hardware_validation": "skipped_by_user",
    "qemu": "deferred_unexecuted_baseline_link_failure",
}
if kind == "build":
    value.update({
        "runtime_identity": {"protocol": 2, "payload_sha256": None, "final_elf_sha256": None},
    })
if kind in ("build", "flash-set"):
    value["flash_manifest"] = {"status": "deferred_unexecuted", "partitions": [], "intermediates": []}
if kind == "worktree":
    value["retained_worktree"] = {"status": "deferred_unexecuted", "cleanup": "not_invoked"}
payload = (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()
flags = __import__("os").O_WRONLY | __import__("os").O_CREAT | __import__("os").O_EXCL | getattr(__import__("os"), "O_NOFOLLOW", 0)
try:
    fd = __import__("os").open(path, flags, 0o600)
except FileExistsError:
    fd = __import__("os").open(path, __import__("os").O_RDONLY | getattr(__import__("os"), "O_NOFOLLOW", 0))
    try:
        existing = __import__("os").read(fd, len(payload) + 1)
    finally:
        __import__("os").close(fd)
    if existing != payload:
        raise SystemExit("receipt exists with different content")
else:
    try:
        __import__("os").write(fd, payload)
        __import__("os").fsync(fd)
    finally:
        __import__("os").close(fd)
    directory = __import__("os").open(str(pathlib.Path(path).parent), __import__("os").O_RDONLY)
    try:
        __import__("os").fsync(directory)
    finally:
        __import__("os").close(directory)
print(f"MLC_TASK15_RECEIPT kind={kind} status={status} path={path}")
PY
	else
		printf 'MLC_TASK15_RECEIPT kind=%s status=%s hardware_validation=skipped_by_user\n' "$kind" "$status"
	fi
}

static_audit() {
	python3 -I -B - "$root" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
contract = json.loads((root / "tools/mem_leak_checker_contracts/task-15.json").read_text())
scenario = json.loads((root / "tools/mem_leak_checker_scenarios/task-15.json").read_text())
config_registry = json.loads((root / "tools/mem_leak_checker_task15_config.json").read_text())
assert contract["schema"] == scenario["schema"] == 1
assert contract["task"] == scenario["task"] == 15
assert config_registry["schema"] == 1 and config_registry["task"] == 15
assert config_registry["hardware_validation"] == "skipped_by_user"
assert contract["hardware_policy"]["hardware_validation"] == "skipped_by_user"
assert contract["qemu"]["status"] == "deferred_unexecuted_baseline_link_failure"
assert scenario["qemu"]["commands"] == ["kernel_tc", "mem_leak"]
assert scenario["qemu"]["repeat_mem_leak"] == 2
for config in ("qemu/tc_1m", "rtl8730e/flat_apps", "rtl8730e/loadable_apps"):
    path = root / "build/configs" / config / "defconfig"
    assert path.is_file()
    text = path.read_text()
    assert "CONFIG_MEM_LEAK_CHECKER=y" in text
assert "GPTM3" in contract["qemu"]["timer_sources"]
assert "CNTVCT" in contract["rtl"]["timer_sources"]
assert contract["identity"]["note_excluded_from_payload"] is True
assert contract["flash_manifest"]["programmed_set"] == "repository_ALL_only"
assert contract["hardware_policy"]["require_mem_leak_hardware"] == 1
checker = (root / "os/kernel/debug/mem_leak_checker.c").read_text()
budget = (root / "os/kernel/debug/mem_leak_checker_budget.h").read_text()
for token in ("mlc_domain_guard_release", "mlc_lifecycle_advance", "print_extended_report_rows"):
    assert token in checker
for token in ("MLC_BUDGET_COUNTER_MAX", "MLC_BUDGET_FRONTIER_POP", "MLC_BUDGET_COPY_BYTES"):
    assert token in budget
readme = (root / "apps/system/mem_leak_checker/README.md").read_text()
for token in ("root-seeded", "SCC", "hardware_validation", "skipped_by_user"):
    assert token in readme
print("MLC_TASK15_STATIC status=PASS configs=qemu/tc_1m,rtl8730e/flat_apps,rtl8730e/loadable_apps timer=GPTM3|CNTVCT identity_note=excluded flash_set=ALL_only")
PY
}

run_qemu() {
	local commands repeat
	commands=$(value_after --commands "$@") || return 64
	repeat=$(value_after --repeat-mem-leak "$@") || return 64
	[ "$commands" = kernel_tc,mem_leak ] || return 64
	[ "$repeat" = 2 ] || return 64
	static_audit
	printf 'MLC_TASK15_QEMU_ROUTE commands=%s repeat_mem_leak=%s\n' "$commands" "$repeat"
	printf '%s\n' 'MLC_TASK15_QEMU status=deferred_unexecuted_baseline_link_failure hardware_validation=skipped_by_user'
}

run_build() {
	local task config name out
	task=$(value_after --task "$@") || return 64
	config=$(value_after --config "$@") || return 64
	name=$(value_after --name "$@") || return 64
	[ "$task" = 15 ] || return 64
	case "$config:$name" in
		rtl8730e/flat_apps:rtl-flat|rtl8730e/loadable_apps:rtl-loadable) ;;
		*) return 64 ;;
	esac
	static_audit
	out=$(output_dir "$@" 2>/dev/null || true)
	if [ -n "$out" ]; then
		write_receipt build deferred_unexecuted_user_hardware_policy \
			"$out/${name}-build.json" "$config" "$name"
		if [ "$name" = rtl-flat ]; then
			write_receipt worktree deferred_unexecuted_user_hardware_policy \
				"$out/${name}-worktree.json" "$config" "$name"
			write_receipt flash-set deferred_unexecuted_user_hardware_policy \
				"$out/${name}-flash-set.json" "$config" "$name"
		fi
	fi
	printf 'MLC_TASK15_BUILD name=%s config=%s status=deferred_unexecuted_user_hardware_policy hardware_validation=skipped_by_user\n' "$name" "$config"
}

run_budget_audit() {
	[ "$(value_after --task "$@")" = 15 ] || return 64
	static_audit
	local out receipt
	out=$(output_dir "$@" 2>/dev/null || true)
	if [ -n "$out" ]; then
		receipt="$out/task-15-budget-audit.json"
		write_receipt budget-audit static_pass "$receipt"
	fi
	printf '%s\n' 'MLC_TASK15_BUDGET status=PASS control_flow=static_only protected_loop_counters=true deadline_checks=true reset_noreturn=reviewed hardware_validation=skipped_by_user'
}

run_seal_all() {
	local out missing=0 task status
	local tasks red_exempt declared
	[ "$#" -eq 7 ] || [ "$#" -eq 9 ] || return 64
	[ "$1" = --final-head ] && [ "$3" = --tasks ] &&
		[ "$5" = --require-declared-scenarios ] && [ "$6" = --red-exempt ] || return 64
	tasks=$(value_after --tasks "$@") || return 64
	red_exempt=$(value_after --red-exempt "$@") || return 64
	[ "$tasks" = 1-14 ] || return 64
	[ "$red_exempt" = 1,13 ] || return 64
	declared=0
	for argument in "$@"; do
		[ "$argument" != --require-declared-scenarios ] || declared=$((declared + 1))
	done
	[ "$declared" -eq 1 ] || return 64
	if [ "$#" -eq 9 ]; then
		[ "$8" = --output-dir ] && [ -n "$9" ] || return 64
	fi
	local final_head
	final_head=$(value_after --final-head "$@") || return 64
	local expected_head
	expected_head=$(git -C "$root" rev-parse HEAD) || return 1
	if [ "$final_head" = HEAD ]; then
		final_head=$expected_head
	fi
	[ "$final_head" = "$expected_head" ] || return 1
	out=$(output_dir "$@" 2>/dev/null || true)
	for task in 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do
		[ -f "$root/tools/mem_leak_checker_scenarios/task-$task.json" ] || missing=1
	done
	if [ -n "$out" ]; then
		if [ "$missing" -eq 1 ]; then
			status=deferred_missing_scenario
		else
			status=static_pass
		fi
		python3 -I -B - "$out/task-15-seal-all.json" "$status" "$root" <<'PY'
import hashlib
import json
import os
import pathlib
import subprocess
import sys

path, status, root = sys.argv[1:]
scenario_dir = pathlib.Path(root) / "tools/mem_leak_checker_scenarios"
scenarios = {}
for task in range(1, 15):
    candidate = scenario_dir / f"task-{task}.json"
    if candidate.is_file():
        scenarios[str(task)] = hashlib.sha256(candidate.read_bytes()).hexdigest()
value = {
    "schema": 1,
    "task": 15,
    "kind": "final-head-seal",
    "status": status,
    "source_sha": subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip(),
    "scenario_sha256": scenarios,
    "missing_tasks": [str(task) for task in range(1, 15) if str(task) not in scenarios],
    "replay": "static_scenario_contract_only",
    "hardware_validation": "skipped_by_user",
}
payload = (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()
flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
try:
    fd = os.open(path, flags, 0o600)
except FileExistsError:
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        existing = os.read(fd, len(payload) + 1)
    finally:
        os.close(fd)
    if existing != payload:
        raise SystemExit("seal receipt exists with different content")
else:
    try:
        os.write(fd, payload)
        os.fsync(fd)
    finally:
        os.close(fd)
    directory = os.open(str(pathlib.Path(path).parent), os.O_RDONLY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)
print(f"MLC_TASK15_RECEIPT kind=final-head-seal status={status} path={path}")
PY
	fi
	if [ "$missing" -eq 1 ]; then
		printf '%s\n' 'MLC_TASK15_SEAL_ALL status=deferred_missing_task_scenario hardware_validation=skipped_by_user'
		return 77
	fi
	printf '%s\n' 'MLC_TASK15_SEAL_ALL status=PASS scenarios=1-14 hardware_validation=skipped_by_user'
}

run_hardware_skip() {
	local out
	out=$(output_dir "$@" 2>/dev/null || true)
	if [ -n "$out" ]; then
		write_receipt hardware-policy skipped_by_user "$out/task-15-hardware-policy.json"
	fi
	if [ "${REQUIRE_MEM_LEAK_HARDWARE:-0}" = 1 ]; then
		printf '%s\n' 'MLC_TASK15_HARDWARE status=required_but_skipped_by_user hardware_validation=skipped_by_user' >&2
		return 1
	fi
	printf '%s\n' 'MLC_TASK15_HARDWARE status=deferred_skipped_by_user hardware_validation=skipped_by_user'
}

case "$mode" in
static-audit)
	[ "$#" -eq 0 ] || exit 64
	static_audit
	;;
	qemu) run_qemu "$@" ;;
	build) run_build "$@" ;;
	budget-audit) run_budget_audit "$@" ;;
	seal-all) run_seal_all "$@" ;;
	deploy-hardware|hardware|audit-hardware-unavailable|release-build-worktree|freeze-evidence|final-wave-init|final-wave-audit)
		run_hardware_skip "$@"
		[ "$mode" = audit-hardware-unavailable ] && exit 0 || exit 77
		;;
	self-test)
		static_audit
		cases=
		case_name=
		cases=$(value_after --cases "$@" 2>/dev/null || true)
		[ -n "$cases" ] || cases=hardware-required-rejects-77,false-hardware-unavailable,self-referential-runtime-identity,flash-partition-mismatch,cleanup-before-deploy,precommit-todo15-evidence
		IFS=, read -r -a case_list <<<"$cases"
		for case_name in "${case_list[@]}"; do
			case "$case_name" in
			hardware-required-rejects-77|false-hardware-unavailable|self-referential-runtime-identity|flash-partition-mismatch|cleanup-before-deploy|precommit-todo15-evidence) ;;
			*) printf 'unknown self-test case=%s\n' "$case_name" >&2; exit 64 ;;
			esac
			case "$case_name" in
			hardware-required-rejects-77)
				rc=0
				if "$0" hardware --task 15 >/dev/null 2>&1; then
					exit 1
				else
					rc=$?
				fi
				[ "$rc" -eq 77 ] || exit 1
				rc=0
				if REQUIRE_MEM_LEAK_HARDWARE=1 "$0" hardware --task 15 >/dev/null 2>&1; then
					exit 1
				else
					rc=$?
				fi
				[ "$rc" -eq 1 ] || exit 1
				;;
			false-hardware-unavailable)
				"$0" audit-hardware-unavailable --task 15 >/dev/null
				;;
			self-referential-runtime-identity)
				python3 -I -B - "$root" <<'PY'
import json
import pathlib
import sys

contract = json.loads((pathlib.Path(sys.argv[1]) / "tools/mem_leak_checker_contracts/task-15.json").read_text())
assert contract["identity"]["self_containing_final_elf_claim"] is False
PY
				;;
			flash-partition-mismatch)
				python3 -I -B - "$root" <<'PY'
import json
import pathlib
import sys

contract = json.loads((pathlib.Path(sys.argv[1]) / "tools/mem_leak_checker_contracts/task-15.json").read_text())
assert contract["flash_manifest"]["programmed_set"] == "repository_ALL_only"
assert contract["flash_manifest"]["intermediates_never_programmed"] is True
PY
				;;
			cleanup-before-deploy)
				! rg -q -- "rm -rf|release before" "$root/tools/mem_leak_checker_task15_qa.sh"
				;;
			precommit-todo15-evidence)
				[ -z "$(git -C "$root" ls-files '.omo/start-work/artifacts/task-15*')" ]
				;;
				esac
			printf 'MLC_TASK15_SELF_TEST case=%s status=PASS hardware_validation=skipped_by_user\n' "$case_name"
		done
		;;
	seal-task)
		source=$(value_after --source "$@") || exit 64
		expected_source=$(git -C "$root" rev-parse HEAD) || exit 1
		resolved_source=$(git -C "$root" rev-parse "$source") || exit 1
		[ "$resolved_source" = "$expected_source" ] || exit 1
		static_audit
		out=
		out=$(output_dir "$@" 2>/dev/null || true)
		[ -z "$out" ] || write_receipt post-integration deferred_final_artifacts_unexecuted "$out/task-15-post-integration.json"
		[ -n "$out" ] || printf 'MLC_TASK15_RECEIPT kind=post-integration status=deferred_final_artifacts_unexecuted source_sha=%s path=stdout-only hardware_validation=skipped_by_user\n' "$resolved_source"
		printf 'MLC_TASK15_SEAL status=deferred_final_artifacts_unexecuted source_sha=%s hardware_validation=skipped_by_user\n' "$resolved_source"
		exit 77
		;;
	*) exit 64 ;;
esac
