#!/usr/bin/env bash
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
fixture_parent=$(mktemp -d /tmp/mlc-task5-seal-test.XXXXXX)
fixture_root="$fixture_parent/repository"
old_receipt_relative=.omo/start-work/artifacts/task-5-executor/task-5-post-integration.json
old_receipt="$fixture_root/$old_receipt_relative"
old_task4_receipt_relative=.omo/start-work/artifacts/task-4-executor/task-4-post-integration/task-4-post-integration.json
old_task4_receipt="$fixture_root/$old_task4_receipt_relative"
work_id=mem-leak-checker-hardening-257754dc
session=codex:257754dc-1fe6-461f-86d4-57a7d2b9fb8d
plan_fixture=${MLC_TASK5_PLAN_FIXTURE:-$source_root/.omo/plans/mem-leak-checker-hardening.md}

[ -f "$plan_fixture" ] || {
	printf 'Task5 seal self-test plan fixture is missing: %s\n' "$plan_fixture" >&2
	exit 1
}

cleanup() {
	case "$fixture_parent" in
		/tmp/mlc-task5-seal-test.*) [ ! -e "$fixture_parent" ] || find "$fixture_parent" -depth -delete ;;
	esac
}
trap cleanup EXIT

expect_rejected() {
	name=$1
	shift
	if "$@" >"$fixture_parent/$name.out" 2>&1; then
		printf 'Task5 seal self-test accepted %s\n' "$name" >&2
		exit 1
	fi
}

expect_plan_rejected() {
	name=$1
	shift
	expect_rejected "$name" "$@"
	grep -qx 'Task5 active plan content drift' "$fixture_parent/$name.out" || {
		printf 'Task5 seal self-test rejected %s for the wrong reason\n' "$name" >&2
		exit 1
	}
}

wait_for_path() {
	python3 - "$1" <<'PY'
import pathlib
import sys
import time

path = pathlib.Path(sys.argv[1])
deadline = time.monotonic() + 30
while not path.exists():
    if time.monotonic() >= deadline:
        raise SystemExit(f"timed out waiting for {path}")
    time.sleep(0.001)
PY
}

mkdir -p "$fixture_root/tools/mem_leak_checker_scenarios"
mkdir -p "$fixture_root/tools/mem_leak_checker_task5_stubs/arch"
mkdir -p "$fixture_root/os/kernel/irq" "$fixture_root/os/kernel/debug/tests"
git -C "$fixture_root" init -q
git -C "$fixture_root" config user.name task5-seal-test
git -C "$fixture_root" config user.email task5-seal-test@example.invalid
baseline=c93078ab05bb6463467669fb6ee19bb75ee7eaba
git -C "$fixture_root" fetch -q "$source_root" "$baseline"
git -C "$fixture_root" fetch -q "$source_root" 37829f7e4b8cbb3948f1451d5c693be857c551f6
git -C "$fixture_root" symbolic-ref HEAD refs/heads/codex/mem-leak-checker-hardening
git -C "$fixture_root" update-ref refs/heads/codex/mem-leak-checker-hardening "$baseline"
printf '%s\n' \
	'*.ignored' \
	'*.log' \
	'.codegraph/.gitignore' \
	'.codegraph/*.db' \
	'.codegraph/*.db-wal' \
	'.codegraph/*.db-shm' >"$fixture_root/.gitignore"

cp "$source_root/tools/mem_leak_checker_task5_scenarios.sh" "$fixture_root/tools/"
cp "$source_root/tools/mem_leak_checker_qa.sh" "$fixture_root/tools/"
cp "$source_root/tools/mem_leak_checker_task4_qa.sh" "$fixture_root/tools/"
cp "$source_root/tools/mem_leak_checker_task4_authoritative.py" "$fixture_root/tools/"
cp "$source_root/tools/mem_leak_checker_task4_evidence.py" "$fixture_root/tools/"
cp "$source_root/tools/mem_leak_checker_task4_scenarios.py" "$fixture_root/tools/"
cp "$source_root/tools/mem_leak_checker_qa_core.py" "$fixture_root/tools/"
cp "$source_root/tools/mem_leak_checker_scenarios/task-4.json" \
	"$fixture_root/tools/mem_leak_checker_scenarios/"
cp "$source_root/tools/mem_leak_checker_scenarios/task-5.json" \
	"$fixture_root/tools/mem_leak_checker_scenarios/"
cat >"$fixture_root/tools/test_mem_leak_checker_graph.sh" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
fixtures=
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
while [ "$#" -gt 0 ]; do
	case "$1" in
		--fixtures) fixtures=$2; shift 2 ;;
		--repeat) shift 2 ;;
		*) exit 64 ;;
	esac
done
if [ -z "$(git -C "$root" branch --show-current)" ]; then
	printf '%s\n' 'Task4 fixture expected RED'
	exit 1
fi
case "$fixtures" in
	mlc_graph_core,mlc_zero_graph)
		printf '%s\n' 'MLC_HOST fixture=mlc_graph_core status=PASS' 'MLC_HOST fixture=mlc_zero_graph status=PASS' ;;
	mlc_frontier_tarjan_exhaustion,mlc_tarjan_max_depth)
		printf '%s\n' \
			'MLC_HOST fixture=mlc_frontier_tarjan_exhaustion status=PASS verdict=INCOMPLETE_CAPACITY rows=0 canaries=intact' \
			'MLC_HOST fixture=mlc_tarjan_max_depth status=PASS depth=3000 recursion=none' ;;
	*) exit 64 ;;
esac
STUB
cat >"$fixture_root/tools/mem_leak_checker_task5_qa.sh" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
if [ "${MLC_TASK5_MUTATE_DURING_SCENARIOS:-0}" = 1 ] && [ ! -e "$root/mutation.once" ]; then
	printf 'scenario mutation\n' >>"$root/os/kernel/irq/irq_csection.c"
	: >"$root/mutation.once"
fi
if [ "${1:-}" = --red ]; then
	printf '%s\n' 'MLC_TASK5_RED stale_identity_detected=true expected_exit=86'
	exit 86
fi
printf '%s\n' \
 'MLC_TASK5_MODEL status=PASS repeat=100' \
 'MLC_TASK5_LIFECYCLE_FAILURES status=PASS classes=5 phases=8' \
 'MLC_TASK5_PHASE_ORDER status=PASS acquire=domain,critical,heaps unwind=heaps,critical,domain' \
 'MLC_TASK5_PHASE_PREDECESSORS status=PASS missing=critical,heaps,domain_resource skips=domain_heaps,captured,analysis,copied' \
 'MLC_TASK5_REPORT_ATOMIC status=PASS two_heap_late_failure=true capacity_no_partial=true' \
 'MLC_TASK5_ADMISSION_RACE status=PASS repeat=100' \
 'MLC_TASK5_FATAL_ISOLATED status=PASS ownership_retained=true' \
 'MLC_TASK5_LIFECYCLE status=PASS recoverable_reuse=true post_release_record=true' \
 'MLC_TASK5_MIGRATION_BOUNDARY status=PASS published_cpu=1 stale_cpu=0' \
 'MLC_TASK5_IRQ_ACTUAL variant=smp_irqcount status=PASS repeat=100' \
 'MLC_TASK5_IRQ_ACTUAL variant=up_irqcount status=PASS repeat=100' \
 'MLC_TASK5_IRQ_ACTUAL variant=up_no_irqcount status=PASS repeat=100' \
 'MLC_TASK5_VARIANT_COMPILE status=PASS fallback=up_no_irqcount negative=smp_no_irqcount' \
 'MLC_TASK5_STATIC status=PASS variants=3 negative=smp_no_irqcount atomic_report=true phase_order=true'
STUB
chmod +x "$fixture_root/tools/mem_leak_checker_task5_scenarios.sh" \
	"$fixture_root/tools/mem_leak_checker_task5_qa.sh" \
	"$fixture_root/tools/mem_leak_checker_qa.sh" \
	"$fixture_root/tools/mem_leak_checker_task4_qa.sh" \
	"$fixture_root/tools/test_mem_leak_checker_graph.sh"
for path in \
	os/kernel/irq/irq_csection.c \
	os/kernel/debug/mem_leak_checker.c \
	os/kernel/debug/mem_leak_checker_graph.c \
	os/kernel/debug/mem_leak_checker_graph.h \
	os/kernel/debug/mem_leak_checker_graph_internal.h \
	os/kernel/debug/mem_leak_checker_graph_validate.c \
	os/kernel/debug/tests/test_mem_leak_checker_graph.c \
	os/kernel/debug/tests/test_mem_leak_checker_graph_depth.c \
	os/kernel/debug/mem_leak_checker_lifecycle.c \
	os/kernel/debug/mem_leak_checker_lifecycle.h \
	tools/mem_leak_checker_task5_model.c \
	tools/mem_leak_checker_task5_lifecycle_test.c \
	tools/mem_leak_checker_task5_irq_actual_test.c \
	tools/mem_leak_checker_task5_irq_fallback_test.c \
	tools/mem_leak_checker_task5_seal_test.sh \
	tools/mem_leak_checker_task5_stubs/arch/irq.h; do
	printf 'Task5 seal fixture: %s\n' "$path" >"$fixture_root/$path"
done

mkdir -p "$fixture_root/.omo/plans"
for task in 1 2 3 4 5; do
	mkdir -p "$fixture_root/.omo/start-work/artifacts/task-${task}-executor"
done
mkdir -p "$fixture_root/.omo/start-work/recovery/preintegrate-todo2-stale"
cp "$plan_fixture" \
	"$fixture_root/.omo/plans/mem-leak-checker-hardening.md"
python3 - "$fixture_root" "$work_id" "$session" "$baseline" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
work_id, session, baseline = sys.argv[2:]
plan = ".omo/plans/mem-leak-checker-hardening.md"
boulder = {
    "schema_version": 2,
    "active_work_id": work_id,
    "works": {work_id: {
        "work_id": work_id,
        "active_plan": plan,
        "plan_name": "mem-leak-checker-hardening",
        "session_ids": [session],
        "status": "active",
        "worktree_path": str(root.resolve()),
    }},
}
(root / ".omo/boulder.json").write_text(json.dumps(boulder))
entries = [{
    "event": "work-started",
    "task": "orchestration",
    "session_id": session,
    "plan": plan,
    "artifact": ".omo/boulder.json",
    "baseline_sha": baseline,
    "plan_sha256": "b8a15c4fe82ae900c8cac49e4b5304276b0c8a5e4809283b9c06a634f644f677",
}, {
    "event": "execution-override",
    "task": "verification-policy",
    "session_id": session,
    "plan": plan,
    "artifact": plan + "#approved-execution-override-2026-07-19",
}, {
    "event": "execution-exception",
    "task": "todo-1-context-and-publication",
    "session_id": session,
    "plan": plan,
    "artifact": plan + "#resumed-worktree-and-evidence-filesystem-exception",
}]
(root / ".omo/start-work/ledger.jsonl").write_text(
    "".join(json.dumps(entry) + "\n" for entry in entries)
)
PY
printf '%s\n' "$baseline" >"$fixture_root/.omo/start-work/artifacts/task-1-executor/baseline.sha"
for task in 2 3 4; do
	printf 'sanctioned task %s evidence\n' "$task" \
		>"$fixture_root/.omo/start-work/artifacts/task-${task}-executor/control.txt"
done
printf 'sanctioned\n' >"$fixture_root/.omo/start-work/artifacts/task-5-executor/red-green.md"
printf 'sanctioned ignored evidence\n' >"$fixture_root/.omo/start-work/artifacts/task-5-executor/model.log"
printf '%s\n' '{"schema":1,"task":5,"status":"legacy-fixed-receipt"}' >"$old_receipt"
mkdir -p "$(dirname "$old_task4_receipt")"
printf '%s\n' '{"schema":1,"task":4,"status":"legacy-fixed-receipt"}' >"$old_task4_receipt"
printf 'sanctioned\n' >"$fixture_root/.omo/start-work/recovery/preintegrate-todo2-stale/state.c"
mkdir -p "$fixture_root/.codegraph"
printf '# live external index\n' >"$fixture_root/.codegraph/.gitignore"
printf 'sqlite db\n' >"$fixture_root/.codegraph/codegraph.db"
printf 'sqlite wal\n' >"$fixture_root/.codegraph/codegraph.db-wal"
printf 'sqlite shm\n' >"$fixture_root/.codegraph/codegraph.db-shm"

python3 - "$fixture_root" <<'PY'
import hashlib
import json
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
raw = subprocess.run(
    ["git", "-C", str(root), "ls-files", "--others", "-z"],
    check=True, capture_output=True,
).stdout
files = []
for item in raw.split(b"\0"):
    if not item:
        continue
    relative = item.decode()
    if re.fullmatch(
        r"\.omo/start-work/(?:artifacts/task-[1-5]-executor/.+|"
        r"recovery/preintegrate-todo[1-5]-stale/.+)", relative
    ):
        files.append({
            "path": relative,
            "sha256": hashlib.sha256((root / relative).read_bytes()).hexdigest(),
        })
value = {"schema": 1, "task": 5, "files": files}
(root / "tools/mem_leak_checker_task5_seal_allowlist.json").write_text(
    json.dumps(value, indent=2, sort_keys=True) + "\n"
)
PY
git -C "$fixture_root" add .gitignore os tools
git -C "$fixture_root" commit -q -m receiving

printf 'tracked Codegraph content\n' >"$fixture_root/.codegraph/tracked.db"
git -C "$fixture_root" add -f .codegraph/tracked.db
git -C "$fixture_root" commit -q -m tracked-codegraph-negative
expect_rejected tracked-codegraph-content \
	"$fixture_root/tools/mem_leak_checker_task5_scenarios.sh" seal --source HEAD
git -C "$fixture_root" update-index --force-remove .codegraph/tracked.db
git -C "$fixture_root" commit -q -m untrack-codegraph

seal_command="$fixture_root/tools/mem_leak_checker_task5_scenarios.sh"
receiving_sha=$(git -C "$fixture_root" rev-parse HEAD)
task4_receipt_relative=".omo/start-work/artifacts/task-4-executor/task-4-post-integration-$receiving_sha.json"
task4_receipt="$fixture_root/$task4_receipt_relative"
receipt_relative=".omo/start-work/artifacts/task-5-executor/task-5-post-integration-$receiving_sha.json"
receipt="$fixture_root/$receipt_relative"
git -C "$fixture_root" switch -q -c codex/mlc-todo4
expect_rejected isolated-task4-context \
	"$fixture_root/tools/mem_leak_checker_qa.sh" seal-task --task 4 --source HEAD
expect_rejected isolated-task4-optimized-context \
	env PYTHONOPTIMIZE=1 "$fixture_root/tools/mem_leak_checker_qa.sh" seal-task --task 4 --source HEAD
git -C "$fixture_root" switch -q codex/mem-leak-checker-hardening
expect_rejected task5-missing-current-task4 "$seal_command" seal --source HEAD
expect_rejected task4-forged-tree-plan-direct \
	python3 "$fixture_root/tools/mem_leak_checker_task4_authoritative.py" \
	"$fixture_root" "$fixture_root/.omo/start-work/artifacts/task-4-executor" \
	"$task4_receipt" "$receiving_sha" "$(printf '0%.0s' {1..40})" "$(printf '0%.0s' {1..64})"
[ ! -e "$task4_receipt" ]
task4_cleanup_marker="$fixture_parent/task4-pre-fstat-cleanup.marker"
MLC_TASK4_PRE_FSTAT_FAILURE=1 MLC_TASK4_CLEANUP_MARKER="$task4_cleanup_marker" \
	expect_rejected task4-pre-fstat-failure \
	"$fixture_root/tools/mem_leak_checker_qa.sh" seal-task --task 4 --source HEAD
grep -q 'injected task-4 pre-fstat failure' "$fixture_parent/task4-pre-fstat-failure.out"
! grep -q 'UnboundLocalError' "$fixture_parent/task4-pre-fstat-failure.out"
[ ! -e "$task4_receipt" ]
[ -f "$task4_cleanup_marker" ]
grep -qx 'owned_receipt_unlinked_directory_fsynced' "$task4_cleanup_marker"
task4_persistent_marker="$fixture_parent/task4-persistent-fstat-cleanup.marker"
MLC_TASK4_PRE_FSTAT_FAILURE=1 MLC_TASK4_CLEANUP_FSTAT_FAILURE=1 \
	MLC_TASK4_CLEANUP_MARKER="$task4_persistent_marker" \
	expect_rejected task4-persistent-fstat-failure \
	"$fixture_root/tools/mem_leak_checker_qa.sh" seal-task --task 4 --source HEAD
grep -q 'injected task-4 pre-fstat failure' "$fixture_parent/task4-persistent-fstat-failure.out"
! grep -q 'UnboundLocalError' "$fixture_parent/task4-persistent-fstat-failure.out"
[ ! -e "$task4_receipt" ]
grep -qx 'owned_receipt_unlinked_directory_fsynced' "$task4_persistent_marker"
task4_foreign_marker="$fixture_parent/task4-foreign-cleanup.marker"
MLC_TASK4_PRE_FSTAT_FOREIGN_SUBSTITUTION=1 \
	MLC_TASK4_CLEANUP_MARKER="$task4_foreign_marker" \
	expect_rejected task4-pre-fstat-foreign-substitution \
	"$fixture_root/tools/mem_leak_checker_qa.sh" seal-task --task 4 --source HEAD
grep -q 'injected task-4 foreign substitution' \
	"$fixture_parent/task4-pre-fstat-foreign-substitution.out"
[ "$(cat "$task4_receipt")" = 'foreign task-4 receipt' ]
[ ! -e "$task4_foreign_marker" ]
find "$task4_receipt" -delete
task4_named_stat_marker="$fixture_parent/task4-named-stat-cleanup.marker"
MLC_TASK4_NAME_STAT_FAILURE=1 MLC_TASK4_CLEANUP_FSTAT_FAILURE=1 \
	MLC_TASK4_CLEANUP_MARKER="$task4_named_stat_marker" \
	expect_rejected task4-named-stat-failure \
	"$fixture_root/tools/mem_leak_checker_qa.sh" seal-task --task 4 --source HEAD
grep -q 'injected task-4 named-stat failure' "$fixture_parent/task4-named-stat-failure.out"
! grep -q 'UnboundLocalError' "$fixture_parent/task4-named-stat-failure.out"
[ -f "$task4_receipt" ]
[ ! -e "$task4_named_stat_marker" ]
find "$task4_receipt" -delete
MLC_TASK4_AFTER_WRITE_MUTATION=1 expect_rejected task4-after-write-mutation \
	"$fixture_root/tools/mem_leak_checker_qa.sh" seal-task --task 4 --source HEAD
[ ! -e "$task4_receipt" ]
git -C "$fixture_root" restore os/kernel/debug/mem_leak_checker_graph.c
direct_evidence=$(mktemp -d "$fixture_parent/task4-direct-evidence.XXXXXX")
"$fixture_root/tools/mem_leak_checker_task4_qa.sh" red-development \
	--evidence-dir "$direct_evidence" >/dev/null
task4_survivor_marker="$fixture_parent/task4-survivor-fsync.marker"
MLC_TASK4_IDENTICAL_SUBSTITUTION=1 MLC_TASK4_SURVIVOR_FSYNC_MARKER="$task4_survivor_marker" \
	python3 "$fixture_root/tools/mem_leak_checker_task4_authoritative.py" \
	"$fixture_root" "$direct_evidence" "$task4_receipt" "$receiving_sha" \
	>"$fixture_parent/task4-direct.out"
[ -f "$task4_receipt" ]
grep -qx 'reopened_named_file_and_directory_fsynced' "$task4_survivor_marker"
python3 - "$fixture_root" "$task4_receipt" <<'PY'
import hashlib
import json
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
receipt = json.loads(pathlib.Path(sys.argv[2]).read_bytes())
tree = subprocess.run(
    ["git", "-C", str(root), "rev-parse", "HEAD^{tree}"],
    check=True,
    capture_output=True,
    text=True,
).stdout.strip()
plan = (root / ".omo/plans/mem-leak-checker-hardening.md").read_bytes()
normalized = re.sub(
    rb"(?m)^- \[(?: |x)\] (?=(?:[0-9]+|F[1-4])\.)",
    b"- [ ] ",
    plan,
)
if receipt["receiving_tree"] != tree:
    raise SystemExit("Task4 direct receipt tree was not independently derived")
if receipt["normalized_plan_sha256"] != hashlib.sha256(normalized).hexdigest():
    raise SystemExit("Task4 direct receipt plan digest was not independently derived")
PY
task4_first_sha=$(shasum -a 256 "$task4_receipt" | awk '{print $1}')
printf 'tree-source mutation\n' >>"$fixture_root/os/kernel/debug/mem_leak_checker_graph.c"
expect_rejected task4-tree-source-mutation \
	python3 "$fixture_root/tools/mem_leak_checker_task4_authoritative.py" \
	"$fixture_root" "$direct_evidence" "$task4_receipt" "$receiving_sha"
[ "$task4_first_sha" = "$(shasum -a 256 "$task4_receipt" | awk '{print $1}')" ]
git -C "$fixture_root" restore os/kernel/debug/mem_leak_checker_graph.c
cp "$fixture_root/.omo/plans/mem-leak-checker-hardening.md" "$fixture_parent/plan-before-mutation.md"
printf '\nplan-source mutation\n' >>"$fixture_root/.omo/plans/mem-leak-checker-hardening.md"
expect_rejected task4-plan-source-mutation \
	python3 "$fixture_root/tools/mem_leak_checker_task4_authoritative.py" \
	"$fixture_root" "$direct_evidence" "$task4_receipt" "$receiving_sha"
[ "$task4_first_sha" = "$(shasum -a 256 "$task4_receipt" | awk '{print $1}')" ]
cp "$fixture_parent/plan-before-mutation.md" "$fixture_root/.omo/plans/mem-leak-checker-hardening.md"
find "$task4_survivor_marker" -delete
MLC_TASK4_SURVIVOR_FSYNC_MARKER="$task4_survivor_marker" \
	"$fixture_root/tools/mem_leak_checker_qa.sh" seal-task --task 4 --source HEAD >"$fixture_parent/task4-first.out"
grep -qx 'reopened_named_file_and_directory_fsynced' "$task4_survivor_marker"
task4_replay_sha=$(shasum -a 256 "$task4_receipt" | awk '{print $1}')
MLC_TASK4_IDENTICAL_FSYNC_FAILURE=1 expect_rejected task4-identical-fsync-failure \
	"$fixture_root/tools/mem_leak_checker_qa.sh" seal-task --task 4 --source HEAD
grep -q 'injected task-4 identical receipt fsync failure' \
	"$fixture_parent/task4-identical-fsync-failure.out"
[ "$task4_replay_sha" = "$(shasum -a 256 "$task4_receipt" | awk '{print $1}')" ]
"$fixture_root/tools/mem_leak_checker_qa.sh" seal-task --task 4 --source HEAD >"$fixture_parent/task4-second.out"
[ "$task4_first_sha" = "$(shasum -a 256 "$task4_receipt" | awk '{print $1}')" ]
cmp "$fixture_parent/task4-first.out" "$fixture_parent/task4-second.out"
[ -f "$old_task4_receipt" ]
MLC_TASK5_MUTATE_DURING_SCENARIOS=1 expect_rejected mutation-during-scenarios \
	"$seal_command" seal --source HEAD
[ ! -e "$receipt" ]
git -C "$fixture_root" restore os/kernel/irq/irq_csection.c
find "$fixture_root/mutation.once" -delete
for hook in MLC_TASK5_AFTER_CLEAN_MUTATION MLC_TASK5_BETWEEN_READS_MUTATION; do
	export "$hook=1"
	expect_rejected "$hook" "$seal_command" seal --source HEAD
	unset "$hook"
	[ ! -e "$receipt" ]
	git -C "$fixture_root" restore os/kernel/irq/irq_csection.c
done
MLC_TASK5_FINAL_WINDOW_MUTATION=1 expect_rejected final-window-mutation \
	"$seal_command" seal --source HEAD
[ ! -e "$receipt" ]
git -C "$fixture_root" restore os/kernel/irq/irq_csection.c
task5_cleanup_marker="$fixture_parent/task5-pre-fstat-cleanup.marker"
MLC_TASK5_PRE_FSTAT_FAILURE=1 MLC_TASK5_CLEANUP_MARKER="$task5_cleanup_marker" \
	expect_rejected task5-pre-fstat-failure "$seal_command" seal --source HEAD
grep -q 'injected Task5 pre-fstat failure' "$fixture_parent/task5-pre-fstat-failure.out"
! grep -q 'UnboundLocalError' "$fixture_parent/task5-pre-fstat-failure.out"
[ ! -e "$receipt" ]
[ -f "$task5_cleanup_marker" ]
grep -qx 'owned_receipt_unlinked_directory_fsynced' "$task5_cleanup_marker"
task5_persistent_marker="$fixture_parent/task5-persistent-fstat-cleanup.marker"
MLC_TASK5_PRE_FSTAT_FAILURE=1 MLC_TASK5_CLEANUP_FSTAT_FAILURE=1 \
	MLC_TASK5_CLEANUP_MARKER="$task5_persistent_marker" \
	expect_rejected task5-persistent-fstat-failure "$seal_command" seal --source HEAD
grep -q 'injected Task5 pre-fstat failure' "$fixture_parent/task5-persistent-fstat-failure.out"
! grep -q 'UnboundLocalError' "$fixture_parent/task5-persistent-fstat-failure.out"
[ ! -e "$receipt" ]
grep -qx 'owned_receipt_unlinked_directory_fsynced' "$task5_persistent_marker"
task5_foreign_marker="$fixture_parent/task5-foreign-cleanup.marker"
MLC_TASK5_PRE_FSTAT_FOREIGN_SUBSTITUTION=1 \
	MLC_TASK5_CLEANUP_MARKER="$task5_foreign_marker" \
	expect_rejected task5-pre-fstat-foreign-substitution "$seal_command" seal --source HEAD
grep -q 'injected Task5 foreign substitution' \
	"$fixture_parent/task5-pre-fstat-foreign-substitution.out"
[ "$(cat "$receipt")" = 'foreign Task5 receipt' ]
[ ! -e "$task5_foreign_marker" ]
find "$receipt" -delete
task5_named_stat_marker="$fixture_parent/task5-named-stat-cleanup.marker"
MLC_TASK5_NAME_STAT_FAILURE=1 MLC_TASK5_CLEANUP_FSTAT_FAILURE=1 \
	MLC_TASK5_CLEANUP_MARKER="$task5_named_stat_marker" \
	expect_rejected task5-named-stat-failure "$seal_command" seal --source HEAD
grep -q 'injected Task5 named-stat failure' "$fixture_parent/task5-named-stat-failure.out"
! grep -q 'UnboundLocalError' "$fixture_parent/task5-named-stat-failure.out"
[ -f "$receipt" ]
[ ! -e "$task5_named_stat_marker" ]
find "$receipt" -delete
MLC_TASK5_AFTER_WRITE_MUTATION=1 expect_rejected after-write-mutation \
	"$seal_command" seal --source HEAD
[ ! -e "$receipt" ]
git -C "$fixture_root" restore os/kernel/irq/irq_csection.c
set +e
"$seal_command" red --config qemu/tc_1m --fixture mlc_lifecycle \
	>"$fixture_parent/canonical-red.out" 2>&1
red_status=$?
set -e
[ "$red_status" -eq 86 ]
"$seal_command" qemu \
	--fixtures mlc_lifecycle,mlc_try_critical_fresh_variants,mlc_fake_operation_budget \
	--repeat 100 >"$fixture_parent/canonical-happy.out"
"$seal_command" qemu \
	--fixtures mlc_lifecycle_recoverable_faults,mlc_critical_primary_busy,mlc_critical_secondary_busy,mlc_critical_preowned,mlc_fake_deadline_reserve,mlc_post_release_record_lifetime \
	--repeat 100 >"$fixture_parent/canonical-failure.out"
"$seal_command" qemu \
	--fixtures mlc_lifecycle,mlc_try_critical_fresh_variants,mlc_fake_operation_budget \
	--repeat 100 --post-commit >"$fixture_parent/canonical-post-commit.out"
MLC_TASK5_RECEIPT_FIXTURE_ONLY=1 "$seal_command" receipt-negative \
	>"$fixture_parent/canonical-receipt-negative.out"
PYTHONOPTIMIZE=1 MLC_TASK5_RECEIPT_FIXTURE_ONLY=1 \
	"$seal_command" receipt-negative >"$fixture_parent/optimized-receipt-negative.out"
barrier="$fixture_parent/task5-lock-barrier"
mkdir "$barrier"
set +e
task5_survivor_marker="$fixture_parent/task5-survivor-fsync.marker"
MLC_TASK5_IDENTICAL_SUBSTITUTION=1 MLC_TASK5_SURVIVOR_FSYNC_MARKER="$task5_survivor_marker" \
	MLC_TASK5_LOCK_BARRIER_DIR="$barrier" MLC_TASK5_LOCK_ROLE=holder \
	"$seal_command" seal --source HEAD >"$fixture_parent/first.out" 2>&1 &
first_pid=$!
wait_for_path "$barrier/holder.locked"
MLC_TASK5_LOCK_BARRIER_DIR="$barrier" MLC_TASK5_LOCK_ROLE=contender \
	"$seal_command" seal --source HEAD >"$fixture_parent/second.out" 2>&1 &
second_pid=$!
wait_for_path "$barrier/contender.started"
[ ! -e "$barrier/contender.locked" ]
: >"$barrier/holder.release"
wait "$first_pid"
first_status=$?
wait "$second_pid"
second_status=$?
set -e
[ "$first_status" -eq 0 ] && [ "$second_status" -eq 0 ]
[ -f "$barrier/contender.locked" ]
grep -qx 'reopened_named_file_and_directory_fsynced' "$task5_survivor_marker"
first_receipt_sha=$(shasum -a 256 "$receipt" | awk '{print $1}')
find "$task5_survivor_marker" -delete
MLC_TASK5_SURVIVOR_FSYNC_MARKER="$task5_survivor_marker" \
	"$seal_command" seal --source HEAD >"$fixture_parent/third.out"
grep -qx 'reopened_named_file_and_directory_fsynced' "$task5_survivor_marker"
[ "$first_receipt_sha" = "$(shasum -a 256 "$receipt" | awk '{print $1}')" ]
MLC_TASK5_IDENTICAL_FSYNC_FAILURE=1 expect_rejected task5-identical-fsync-failure \
	"$seal_command" seal --source HEAD
grep -q 'injected Task5 identical receipt fsync failure' \
	"$fixture_parent/task5-identical-fsync-failure.out"
[ "$first_receipt_sha" = "$(shasum -a 256 "$receipt" | awk '{print $1}')" ]
[ -f "$old_receipt" ]
cp "$receipt" "$fixture_parent/concurrent-receipt.json"
find "$receipt" -delete
mkfifo "$receipt"
expect_rejected task5-fifo-winner "$seal_command" seal --source HEAD
find "$receipt" -delete
python3 - "$receipt" <<'PY'
import os
import pathlib
import socket
import sys

target = pathlib.Path(sys.argv[1])
short = pathlib.Path("/tmp") / f"mlc-task5-socket-{os.getpid()}"
endpoint = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
try:
    endpoint.bind(str(short))
finally:
    endpoint.close()
os.rename(short, target)
PY
expect_rejected task5-socket-winner "$seal_command" seal --source HEAD
find "$receipt" -delete
PYTHONPATH="$fixture_root/tools" python3 - <<'PY'
from pathlib import Path

from mem_leak_checker_qa_core import QaError
from mem_leak_checker_task4_evidence import read_regular

try:
    read_regular(Path("/dev/null"))
except QaError:
    pass
else:
    raise SystemExit("Task4 regular-file reader accepted a device")
PY
cp "$fixture_parent/concurrent-receipt.json" "$receipt"
printf '%s\n' '{"foreign":"winner"}' >"$receipt"
expect_rejected task5-foreign-winner "$seal_command" seal --source HEAD
cp "$fixture_parent/concurrent-receipt.json" "$receipt"
python3 - "$task4_receipt" "$receipt" <<'PY'
import json, pathlib, sys
task4 = json.loads(pathlib.Path(sys.argv[1]).read_text())
value = json.loads(pathlib.Path(sys.argv[2]).read_text())
expected_publication = {
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
if task4["publication"] != expected_publication or value["publication"] != expected_publication:
    raise SystemExit("Task4/5 cooperative concurrency contract mismatch")
if value["normalized_plan_sha256"] != "47d8f48a15eaae0f390737bcd3e488948022e3780dcedcf77445de13003f1c93":
    raise SystemExit("Task5 normalized plan digest mismatch")
if value["external_state_exclusions"] != [{
    "path": ".codegraph",
    "reason": "live_external_codegraph_mcp",
    "authenticated": False,
}]:
    raise SystemExit("Task5 external exclusion contract mismatch")
PY

for task in 4 5; do
	case "$task" in
		4) canonical_receipt=$task4_receipt ;;
		5) canonical_receipt=$receipt ;;
	esac
	for version in "$baseline" "$(printf 'f%.0s' {1..40})" arbitrary; do
		version_name="task-$task-post-integration-$version.json"
		version_path="$fixture_root/.omo/start-work/artifacts/task-$task-executor/$version_name"
		cp "$canonical_receipt" "$version_path"
		expect_rejected "version-name-$version_name" "$seal_command" seal --source HEAD
		find "$version_path" -delete
	done
done

cp "$task4_receipt" "$fixture_parent/task4-receipt.json"
for mutation in task sha tree scenario command exit content publication concurrency external-writer cryptographic mutation-scope mac immutable survivor-validation reopened-fsync replay-fsync; do
	python3 - "$task4_receipt" "$mutation" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
mutation = sys.argv[2]
value = json.loads(path.read_text())
if mutation == "task":
    value["task"] = 5
elif mutation == "sha":
    value["receiving_sha"] = "0" * 40
elif mutation == "tree":
    value["receiving_tree"] = "0" * 40
elif mutation == "scenario":
    value["scenario_sha256"] = "0" * 64
elif mutation == "command":
    value["scenario_commands"]["happy"].pop()
elif mutation == "exit":
    value["scenario_exits"][0]["exit"] = 0
elif mutation == "content":
    value["content_sha256"] = "0" * 64
elif mutation == "publication":
    value["publication"]["file_fsync"] = False
elif mutation == "concurrency":
    value["publication"]["concurrency_scope"] = "all_writers"
elif mutation == "external-writer":
    value["publication"]["external_writer_authenticated"] = True
elif mutation == "cryptographic":
    value["publication"]["cryptographic_authentication"] = True
elif mutation == "mutation-scope":
    value["publication"]["external_writer_mutation_scope"] = "all_mutation_prevented"
elif mutation == "mac":
    value["publication"]["mac_authenticated"] = True
elif mutation == "immutable":
    value["publication"]["immutable"] = True
elif mutation == "survivor-validation":
    value["publication"]["surviving_identity_validation"] = "original_descriptor_only"
elif mutation == "reopened-fsync":
    value["publication"]["reopened_named_payload_fsync"] = False
else:
    value["publication"]["idempotent_replay_fsync"] = False
path.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")
PY
	mutant_sha=$(shasum -a 256 "$task4_receipt" | awk '{print $1}')
	expect_rejected "task4-receipt-$mutation" "$seal_command" seal --source HEAD
	[ "$mutant_sha" = "$(shasum -a 256 "$task4_receipt" | awk '{print $1}')" ]
	cp "$fixture_parent/task4-receipt.json" "$task4_receipt"
done

cp "$receipt" "$fixture_parent/versioned-task5-receipt.json"
for mutation in task sha tree scenario command exit content publication concurrency external-writer cryptographic mutation-scope mac immutable survivor-validation reopened-fsync replay-fsync; do
	python3 - "$receipt" "$mutation" <<'PY'
import json
import pathlib
import sys
path = pathlib.Path(sys.argv[1])
mutation = sys.argv[2]
value = json.loads(path.read_text())
if mutation == "task":
    value["task"] = 4
elif mutation == "sha":
    value["receiving_sha"] = "0" * 40
elif mutation == "tree":
    value["receiving_tree"] = "0" * 40
elif mutation == "scenario":
    value["scenario_sha256"] = "0" * 64
elif mutation == "command":
    value["scenario_commands"].pop("happy")
elif mutation == "exit":
    value["scenario_exits"]["happy"] = 1
elif mutation == "content":
    value["content_sha256"] = "0" * 64
elif mutation == "publication":
    value["publication"]["directory_fsync"] = False
elif mutation == "concurrency":
    value["publication"]["concurrency_scope"] = "all_writers"
elif mutation == "external-writer":
    value["publication"]["external_writer_authenticated"] = True
elif mutation == "cryptographic":
    value["publication"]["cryptographic_authentication"] = True
elif mutation == "mutation-scope":
    value["publication"]["external_writer_mutation_scope"] = "all_mutation_prevented"
elif mutation == "mac":
    value["publication"]["mac_authenticated"] = True
elif mutation == "immutable":
    value["publication"]["immutable"] = True
elif mutation == "survivor-validation":
    value["publication"]["surviving_identity_validation"] = "original_descriptor_only"
elif mutation == "reopened-fsync":
    value["publication"]["reopened_named_payload_fsync"] = False
else:
    value["publication"]["idempotent_replay_fsync"] = False
path.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")
PY
	mutant_sha=$(shasum -a 256 "$receipt" | awk '{print $1}')
	expect_rejected "task5-versioned-$mutation" "$seal_command" seal --source HEAD
	[ "$mutant_sha" = "$(shasum -a 256 "$receipt" | awk '{print $1}')" ]
	cp "$fixture_parent/versioned-task5-receipt.json" "$receipt"
done

for task in 4 5; do
	case "$task" in
		4) canonical_receipt=$task4_receipt ;;
		5) canonical_receipt=$receipt ;;
	esac
	cp "$canonical_receipt" "$fixture_parent/task-$task-symlink-source.json"
	find "$canonical_receipt" -delete
	ln -s "$fixture_parent/task-$task-symlink-source.json" "$canonical_receipt"
	expect_rejected "task-$task-receipt-symlink" "$seal_command" seal --source HEAD
	find "$canonical_receipt" -delete
	cp "$fixture_parent/task-$task-symlink-source.json" "$canonical_receipt"
done

cp "$fixture_root/.omo/plans/mem-leak-checker-hardening.md" "$fixture_parent/plan.md"
cp "$fixture_root/.omo/start-work/ledger.jsonl" "$fixture_parent/ledger.jsonl"
for progress in all-open all-closed alternating; do
	python3 - "$fixture_root/.omo/plans/mem-leak-checker-hardening.md" \
		"$fixture_root/.omo/start-work/ledger.jsonl" "$progress" "$session" <<'PY'
import json
import pathlib
import re
import sys

plan_path = pathlib.Path(sys.argv[1])
ledger_path = pathlib.Path(sys.argv[2])
progress = sys.argv[3]
session = sys.argv[4]
index = 0

def replace(match):
    global index
    if progress == "all-open":
        marker = b" "
    elif progress == "all-closed":
        marker = b"x"
    else:
        marker = b"x" if index % 2 else b" "
    index += 1
    return b"- [" + marker + b"] "

plan_path.write_bytes(re.sub(
    rb"(?m)^- \[(?: |x)\] (?=(?:[0-9]+|F[1-4])\.)",
    replace,
    plan_path.read_bytes(),
))
entry = {
    "event": "task-progress",
    "task": progress,
    "session_id": session,
    "plan": ".omo/plans/mem-leak-checker-hardening.md",
    "artifact": ".omo/start-work/artifacts/task-5-executor/red-green.md",
}
with ledger_path.open("a", encoding="utf-8") as ledger:
    ledger.write(json.dumps(entry) + "\n")
PY
	"$seal_command" seal --source HEAD >"$fixture_parent/progress-$progress.out"
	cp "$fixture_parent/plan.md" \
		"$fixture_root/.omo/plans/mem-leak-checker-hardening.md"
	cp "$fixture_parent/ledger.jsonl" "$fixture_root/.omo/start-work/ledger.jsonl"
done

for mutation in body override command whitespace task-number task-add task-remove nested-checkbox; do
	python3 - "$fixture_parent/plan.md" \
		"$fixture_root/.omo/plans/mem-leak-checker-hardening.md" "$mutation" <<'PY'
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
destination = pathlib.Path(sys.argv[2])
mutation = sys.argv[3]
if mutation == "body":
    changed = source.replace("A conservative reachability-based checker", "A permissive reachability-based checker", 1)
elif mutation == "override":
    changed = source.replace("no physical-hardware validation", "physical-hardware validation", 1)
elif mutation == "command":
    changed = source.replace("tools/mem_leak_checker_qa.sh qemu --task 1", "tools/mem_leak_checker_qa.sh qemu --task 99", 1)
elif mutation == "whitespace":
    changed = source.replace("# mem-leak-checker-hardening - Work Plan\n", "# mem-leak-checker-hardening - Work Plan \n", 1)
elif mutation == "task-number":
    changed = re.sub(r"(?m)^(- \[[ x]\] )1\.", r"\g<1>101.", source, count=1)
elif mutation == "task-add":
    changed = source + "\n- [ ] 16. Unauthorized added task\n"
elif mutation == "task-remove":
    changed = re.sub(r"(?m)^- \[[ x]\] 15\..*\n", "", source, count=1)
else:
    changed = source.replace(
        "- [x] 1. Bootstrap deterministic QA",
        "- [x] 1. Bootstrap deterministic QA\n  - [x] Unauthorized nested acceptance checkbox",
        1,
    )
if changed == source:
    raise SystemExit(f"Task5 plan mutation did not change input: {mutation}")
destination.write_text(changed, encoding="utf-8")
PY
	expect_plan_rejected "plan-$mutation" "$seal_command" seal --source HEAD
done
cp "$fixture_parent/plan.md" "$fixture_root/.omo/plans/mem-leak-checker-hardening.md"

python3 - "$fixture_root/.codegraph" "$fixture_parent/codegraph-real" <<'PY'
import os, sys
os.rename(sys.argv[1], sys.argv[2])
os.symlink(sys.argv[2], sys.argv[1])
PY
expect_rejected codegraph-symlink-root "$seal_command" seal --source HEAD
find "$fixture_root/.codegraph" -delete
python3 - "$fixture_parent/codegraph-real" "$fixture_root/.codegraph" <<'PY'
import os, sys
os.rename(sys.argv[1], sys.argv[2])
PY

ln -s "$fixture_parent/outside" "$fixture_root/.codegraph/escape.db"
expect_rejected codegraph-path-escape "$seal_command" seal --source HEAD
find "$fixture_root/.codegraph/escape.db" -delete

printf 'not ignored\n' >"$fixture_root/.codegraph/nonignored.txt"
expect_rejected codegraph-nonignored "$seal_command" seal --source HEAD
find "$fixture_root/.codegraph/nonignored.txt" -delete

mkdir -p "$fixture_root/.codegraph-lookalike"
printf 'lookalike\n' >"$fixture_root/.codegraph-lookalike/state.ignored"
expect_rejected codegraph-lookalike "$seal_command" seal --source HEAD
find "$fixture_root/.codegraph-lookalike" -depth -delete

printf 'surplus\n' >"$fixture_root/surplus.txt"
expect_rejected untracked-product "$seal_command" seal --source HEAD
find "$fixture_root/surplus.txt" -delete

printf 'ignored surplus\n' >"$fixture_root/surplus.ignored"
expect_rejected ignored-untracked-product "$seal_command" seal --source HEAD
find "$fixture_root/surplus.ignored" -delete

printf 'surplus\n' >"$fixture_root/.omo/arbitrary.txt"
expect_rejected arbitrary-omo "$seal_command" seal --source HEAD
find "$fixture_root/.omo/arbitrary.txt" -delete

for task in 1 2 3 4 5; do
	arbitrary="$fixture_root/.omo/start-work/artifacts/task-${task}-executor/arbitrary.txt"
	printf 'arbitrary task artifact\n' >"$arbitrary"
	expect_rejected "arbitrary-task-${task}-artifact" "$seal_command" seal --source HEAD
	find "$arbitrary" -delete
done
recovery_arbitrary="$fixture_root/.omo/start-work/recovery/preintegrate-todo2-stale/arbitrary.txt"
printf 'arbitrary recovery artifact\n' >"$recovery_arbitrary"
expect_rejected arbitrary-recovery-artifact "$seal_command" seal --source HEAD
find "$recovery_arbitrary" -delete

cp "$fixture_root/.omo/start-work/artifacts/task-5-executor/red-green.md" \
	"$fixture_parent/red-green.md"
printf 'content drift\n' >>"$fixture_root/.omo/start-work/artifacts/task-5-executor/red-green.md"
expect_rejected authenticated-artifact-content-drift "$seal_command" seal --source HEAD
cp "$fixture_parent/red-green.md" \
	"$fixture_root/.omo/start-work/artifacts/task-5-executor/red-green.md"

printf 'dirty\n' >>"$fixture_root/os/kernel/irq/irq_csection.c"
expect_rejected tracked-diff "$seal_command" seal --source HEAD
git -C "$fixture_root" restore os/kernel/irq/irq_csection.c

printf 'staged\n' >>"$fixture_root/os/kernel/irq/irq_csection.c"
git -C "$fixture_root" add os/kernel/irq/irq_csection.c
expect_rejected staged-diff "$seal_command" seal --source HEAD
git -C "$fixture_root" restore --staged os/kernel/irq/irq_csection.c
git -C "$fixture_root" restore os/kernel/irq/irq_csection.c

ln -s "$fixture_parent/outside" \
	"$fixture_root/.omo/start-work/artifacts/task-5-executor/path-escape"
expect_rejected symlink-path-escape "$seal_command" seal --source HEAD
find "$fixture_root/.omo/start-work/artifacts/task-5-executor/path-escape" -delete

expect_rejected source-drift "$seal_command" seal --source HEAD^
cp "$fixture_root/.omo/boulder.json" "$fixture_parent/boulder.json"
python3 - "$fixture_root/.omo/boulder.json" "$work_id" <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1])
value = json.loads(path.read_text())
value["works"][sys.argv[2]]["worktree_path"] = "/stale"
path.write_text(json.dumps(value))
PY
expect_rejected stale-context "$seal_command" seal --source HEAD
cp "$fixture_parent/boulder.json" "$fixture_root/.omo/boulder.json"

printf '\nplan drift\n' >>"$fixture_root/.omo/plans/mem-leak-checker-hardening.md"
expect_rejected plan-drift "$seal_command" seal --source HEAD
cp "$fixture_parent/plan.md" "$fixture_root/.omo/plans/mem-leak-checker-hardening.md"

valid_other_ancestor=$(git -C "$fixture_root" rev-parse "$baseline^")
python3 - "$fixture_root/.omo/start-work/ledger.jsonl" "$valid_other_ancestor" <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1])
entries = [json.loads(line) for line in path.read_text().splitlines()]
entries[0]["baseline_sha"] = sys.argv[2]
path.write_text("".join(json.dumps(entry) + "\n" for entry in entries))
PY
expect_rejected different-valid-ancestor-baseline "$seal_command" seal --source HEAD
cp "$fixture_parent/ledger.jsonl" "$fixture_root/.omo/start-work/ledger.jsonl"

python3 - "$fixture_root/.omo/start-work/ledger.jsonl" <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1])
entries = [json.loads(line) for line in path.read_text().splitlines()]
entries[0]["baseline_sha"] = "0" * 40
path.write_text("".join(json.dumps(entry) + "\n" for entry in entries))
PY
expect_rejected baseline-drift "$seal_command" seal --source HEAD
cp "$fixture_parent/ledger.jsonl" "$fixture_root/.omo/start-work/ledger.jsonl"

cp "$fixture_root/.omo/start-work/artifacts/task-1-executor/baseline.sha" \
	"$fixture_parent/baseline.sha"
printf '%s\n' "$valid_other_ancestor" \
	>"$fixture_root/.omo/start-work/artifacts/task-1-executor/baseline.sha"
expect_rejected durable-baseline-file-drift "$seal_command" seal --source HEAD
cp "$fixture_parent/baseline.sha" \
	"$fixture_root/.omo/start-work/artifacts/task-1-executor/baseline.sha"

cp "$receipt" "$fixture_parent/receipt.json"
printf 'receipt drift\n' >"$receipt"
expect_rejected receipt-drift "$seal_command" seal --source HEAD
cp "$fixture_parent/receipt.json" "$receipt"

printf '%s\n' 'MLC_TASK5_SEAL_STATE status=PASS canonical=red,happy,failure,post-commit,receipt-negative idempotent=byte-identical plan_progress=all-open,all-closed,alternating ledger_append=accepted exact_allowlist=true external_codegraph=explicit-unauthenticated rejects=codegraph-symlink-root,codegraph-tracked,codegraph-path-escape,codegraph-nonignored,codegraph-lookalike,unrelated-ignored,untracked-product,arbitrary-omo,task1-5-artifact-arbitrary,recovery-arbitrary,artifact-content-drift,tracked-diff,staged-diff,symlink-path-escape,source-drift,stale-context,plan-body,plan-override,plan-command,plan-whitespace,plan-task-number,plan-task-add,plan-task-remove,plan-nested-checkbox,plan-drift,different-valid-ancestor-baseline,baseline-drift,durable-baseline-file-drift,receipt-drift'
