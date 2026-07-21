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
fixture_parent=
forged_parent=
cleanup_done=0
cleanup() {
	cleanup_status=$?
	trap - EXIT HUP INT TERM
	[ "$cleanup_done" = 0 ] || exit "$cleanup_status"
	cleanup_done=1
	case "$forged_parent" in
		/tmp/mlc-task8-materialized.forge.*|/private/tmp/mlc-task8-materialized.forge.*)
			chmod -R u+w "$forged_parent" 2>/dev/null || true
			find "$forged_parent" -depth -delete ;;
	esac
	forged_parent=
	case "$fixture_parent" in
		/tmp/mlc-task5-seal-test.*|/private/tmp/mlc-task5-seal-test.*)
			chmod -R u+w "$fixture_parent" 2>/dev/null || true
			find "$fixture_parent" -depth -delete ;;
	esac
	exit "$cleanup_status"
}
trap cleanup EXIT
trap 'trap - HUP INT TERM; exit 129' HUP
trap 'trap - HUP INT TERM; exit 130' INT
trap 'trap - HUP INT TERM; exit 143' TERM

source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
/opt/homebrew/bin/python3 -I -B - "$source_root" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
required = (
    "os/kernel/debug/mem_leak_checker_candidates.h",
    "os/kernel/debug/mem_leak_checker_candidates_internal.h",
    "os/kernel/debug/mem_leak_checker_core.h",
    "os/kernel/debug/mem_leak_checker_domain.h",
    "os/kernel/debug/mem_leak_checker_pause.h",
    "os/kernel/debug/mem_leak_checker_pause_owner.h",
)
sources = (
    root / "tools/mem_leak_checker_task8_authoritative.py",
    root / "tools/mem_leak_checker_task5_scenarios.sh",
    root / "tools/mem_leak_checker_task8_qa.sh",
)
for relative in required:
    for source in sources:
        if source.read_text().count(relative) != 1:
            raise SystemExit(f"Task8 materialized dependency mismatch: {relative} in {source.name}")
PY
fixture_parent=$(mktemp -d /tmp/mlc-task5-seal-test.XXXXXX)
fixture_root="$fixture_parent/repository"
plan_fixture=${MLC_TASK8_PLAN_FIXTURE:-$source_root/.omo/plans/mem-leak-checker-hardening.md}
if [ ! -f "$plan_fixture" ]; then
	plan_fixture=$(CDPATH= cd -- "$source_root/../mem-leak-checker-hardening" && pwd -P)/.omo/plans/mem-leak-checker-hardening.md
fi
[ -f "$plan_fixture" ]


reject() {
	name=$1
	shift
	if "$@" >"$fixture_parent/$name.out" 2>&1; then
		printf 'Task8 seal fixture accepted %s\n' "$name" >&2
		exit 1
	fi
}

/usr/bin/git clone -q --no-hardlinks "$source_root" "$fixture_root"
/usr/bin/git -C "$fixture_root" checkout -q -B codex/mem-leak-checker-hardening
/usr/bin/git -C "$fixture_root" config user.name task8-seal-test
/usr/bin/git -C "$fixture_root" config user.email task8-seal-test@example.invalid
for relative in \
	tools/mem_leak_checker_qa.sh \
	tools/mem_leak_checker_scenarios/task-8.json \
	tools/mem_leak_checker_task4_authoritative.py \
	tools/mem_leak_checker_task5_scenarios.sh \
	tools/mem_leak_checker_task8_authoritative.py \
	tools/mem_leak_checker_task8_qa.sh \
	tools/mem_leak_checker_task8_seal_test.sh \
	tools/tests/mem_leak_checker_production_roots_fixture.c \
	tools/tests/mem_leak_checker_task_roots_model.c
do
	mkdir -p "$fixture_root/$(dirname "$relative")"
	cp "$source_root/$relative" "$fixture_root/$relative"
done
chmod +x "$fixture_root/tools/mem_leak_checker_task8_qa.sh" \
	"$fixture_root/tools/mem_leak_checker_task8_seal_test.sh"
printf '%s\n' '.codegraph/*.db' >>"$fixture_root/.gitignore"
/opt/homebrew/bin/python3 -I -B - "$fixture_root" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1]).resolve()
value = {"schema": 1, "task": 5, "files": [{
    "path": ".omo/start-work/artifacts/task-1-executor/baseline.sha",
    "sha256": hashlib.sha256(b"c93078ab05bb6463467669fb6ee19bb75ee7eaba\n").hexdigest(),
}]}
(root / "tools/mem_leak_checker_task5_seal_allowlist.json").write_text(
    json.dumps(value, indent=2, sort_keys=True) + "\n"
)
PY
/usr/bin/git -C "$fixture_root" add .gitignore \
	tools/mem_leak_checker_qa.sh \
	tools/mem_leak_checker_scenarios/task-8.json \
	tools/mem_leak_checker_task4_authoritative.py \
	tools/mem_leak_checker_task5_scenarios.sh \
	tools/mem_leak_checker_task5_seal_allowlist.json \
	tools/mem_leak_checker_task8_authoritative.py \
	tools/mem_leak_checker_task8_qa.sh \
	tools/mem_leak_checker_task8_seal_test.sh \
	tools/tests/mem_leak_checker_production_roots_fixture.c \
	tools/tests/mem_leak_checker_task_roots_model.c
/usr/bin/git -C "$fixture_root" commit -q -m task8-seal-fixture
/usr/bin/git -C "$fixture_root" config --unset-all user.name
/usr/bin/git -C "$fixture_root" config --unset-all user.email
/usr/bin/git -C "$fixture_root" config remote.shared.url ../shared.git
/usr/bin/git -C "$fixture_root" config --add remote.shared.fetch \
	'+refs/heads/*:refs/remotes/shared/*'
/usr/bin/git -C "$fixture_root" config branch.task8-context.remote shared
/usr/bin/git -C "$fixture_root" config branch.task8-context.merge refs/heads/main
receiving_sha=$(/usr/bin/git -C "$fixture_root" rev-parse HEAD)
receiving_tree=$(/usr/bin/git -C "$fixture_root" rev-parse 'HEAD^{tree}')

mkdir -p "$fixture_root/.omo/plans" \
	"$fixture_root/.omo/start-work/artifacts/task-1-executor" \
	"$fixture_root/.omo/start-work/artifacts/task-8-executor" \
	"$fixture_root/.codegraph"
cp "$plan_fixture" "$fixture_root/.omo/plans/mem-leak-checker-hardening.md"
printf '%s\n' c93078ab05bb6463467669fb6ee19bb75ee7eaba > \
	"$fixture_root/.omo/start-work/artifacts/task-1-executor/baseline.sha"
printf 'fixture\n' >"$fixture_root/.codegraph/index.db"
/opt/homebrew/bin/python3 -I -B - "$fixture_root" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1]).resolve()
work = "mem-leak-checker-hardening-257754dc"
session = "codex:257754dc-1fe6-461f-86d4-57a7d2b9fb8d"
plan = ".omo/plans/mem-leak-checker-hardening.md"
boulder = {"schema_version": 2, "active_work_id": work, "works": {work: {
    "work_id": work, "active_plan": plan, "plan_name": "mem-leak-checker-hardening",
    "session_ids": [session], "status": "active", "worktree_path": str(root),
}}}
(root / ".omo/boulder.json").write_text(json.dumps(boulder) + "\n")
entry = {"event": "work-started", "task": "orchestration", "session_id": session,
         "plan": plan, "artifact": ".omo/boulder.json",
         "baseline_sha": "c93078ab05bb6463467669fb6ee19bb75ee7eaba",
         "plan_sha256": hashlib.sha256((root / plan).read_bytes()).hexdigest()}
(root / ".omo/start-work/ledger.jsonl").write_text(json.dumps(entry) + "\n")
PY

if [ "${MLC_TASK8_HOSTILE_REGRESSION_NESTED:-0}" != 1 ]; then
	set -m
	for signal_case in HUP:129 INT:130 TERM:143; do
		signal_name=${signal_case%%:*}
		expected_exit=${signal_case#*:}
		signal_paths="$fixture_parent/signal-$signal_name.paths"
		set +e
		/opt/homebrew/bin/bash "$fixture_root/tools/mem_leak_checker_task8_qa.sh" \
			signal-probe "$signal_name" "$signal_paths" &
		signal_pid=$!
		wait "$signal_pid"
		signal_exit=$?
		set -e
		[ "$signal_exit" = "$expected_exit" ]
		while IFS= read -r owned_path; do
			[ ! -e "$owned_path" ]
		done <"$signal_paths"
	done
	set +m
	printf '%s\n' reached >"$fixture_parent/signal-parent-reached.marker"
	grep -qx reached "$fixture_parent/signal-parent-reached.marker"
	! grep -q 'mem_leak_checker_task8_seal_test.sh' \
		"$fixture_root/tools/mem_leak_checker_task8_qa.sh"
	happy_command="$fixture_root/tools/mem_leak_checker_qa.sh qemu --task 8 --fixtures mlc_task_roots,mlc_direct_wrapper_roots --repeat 1 --post-commit"
	failure_command="$fixture_root/tools/mem_leak_checker_qa.sh qemu --task 8 --fixture mlc_invalid_task_irq_context --repeat 1 --post-commit"
	if ! (cd "$fixture_root" && $happy_command) >"$fixture_parent/happy-clean.out" 2>&1; then
		cat "$fixture_parent/happy-clean.out" >&2
		exit 1
	fi
	if ! (cd "$fixture_root" && $failure_command) >"$fixture_parent/failure-clean.out" 2>&1; then
		cat "$fixture_parent/failure-clean.out" >&2
		exit 1
	fi
	linked_root="$fixture_parent/linked-repository"
	/usr/bin/git -C "$fixture_root" worktree add -q --detach "$linked_root" "$receiving_sha"
	cp -R "$fixture_root/.omo" "$linked_root/.omo"
	mkdir -p "$linked_root/.codegraph"
	cp "$fixture_root/.codegraph/index.db" "$linked_root/.codegraph/index.db"
	/opt/homebrew/bin/python3 -I -B - "$linked_root/.omo/boulder.json" "$linked_root" <<'PY'
import json, pathlib, sys
path, root = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]).resolve()
value = json.loads(path.read_bytes())
value["works"][value["active_work_id"]]["worktree_path"] = str(root)
path.write_text(json.dumps(value) + "\n")
PY
	linked_git_dir=$(/usr/bin/git -C "$linked_root" rev-parse --absolute-git-dir)
	linked_common_dir=$(/usr/bin/git -C "$linked_root" rev-parse --git-common-dir)
	case "$linked_common_dir" in /*) ;; *) linked_common_dir="$linked_root/$linked_common_dir" ;; esac
	linked_object_dir=$(realpath "$linked_common_dir/objects")
	[ "$linked_git_dir" != "$linked_object_dir" ]
	if ! (cd "$linked_root" && \
		"$linked_root/tools/mem_leak_checker_qa.sh" qemu --task 8 \
		--fixtures mlc_task_roots,mlc_direct_wrapper_roots --repeat 1 --post-commit) \
		>"$fixture_parent/linked-worktree.out" 2>&1; then
		cat "$fixture_parent/linked-worktree.out" >&2
		exit 1
	fi
	if ! cmp "$fixture_parent/happy-clean.out" "$fixture_parent/linked-worktree.out"; then
		diff -u "$fixture_parent/happy-clean.out" "$fixture_parent/linked-worktree.out" >&2 || true
		exit 1
	fi
	forged_parent=$(mktemp -d /tmp/mlc-task8-materialized.forge.XXXXXX)
	forged_root="$forged_parent/repository"
	/usr/bin/git clone -q --shared "$linked_root" "$forged_root"
	printf '%s\n' "$linked_git_dir" >"$forged_parent/worktree-gitdir.expected"
	printf '%s\n' "$linked_git_dir" >"$forged_root/.git/objects/info/alternates"
	reject worktree-gitdir-as-object-dir env MLC_TASK8_MATERIALIZED=1 \
		MLC_TASK8_SOURCE_SHA="$receiving_sha" \
		MLC_TASK8_SOURCE_WORKTREE_GIT_DIR="$linked_git_dir" \
		MLC_TASK8_SOURCE_OBJECT_DIR="$linked_git_dir" \
		MLC_TASK8_ALTERNATES_EXPECTED="$forged_parent/worktree-gitdir.expected" \
		"$forged_root/tools/mem_leak_checker_task8_qa.sh" qemu \
		--selector fixtures --fixtures mlc_task_roots,mlc_direct_wrapper_roots \
		--repeat 1 --post-commit
	find "$forged_parent" -depth -delete
	forged_parent=
	/usr/bin/git -C "$fixture_root" worktree remove --force "$linked_root"
	for admin_case in fsmonitor assume-unchanged skip-worktree replace-ref; do
		case "$admin_case" in
			fsmonitor) /usr/bin/git -C "$fixture_root" config core.fsmonitor true ;;
			assume-unchanged) /usr/bin/git -C "$fixture_root" update-index --assume-unchanged tools/mem_leak_checker_scenarios/task-8.json ;;
			skip-worktree) /usr/bin/git -C "$fixture_root" update-index --skip-worktree tools/mem_leak_checker_scenarios/task-8.json ;;
			replace-ref) /usr/bin/git -C "$fixture_root" update-ref "refs/replace/$receiving_sha" "$receiving_sha^" ;;
		esac
		reject "git-admin-$admin_case" $happy_command
		! grep -q PASS "$fixture_parent/git-admin-$admin_case.out"
		case "$admin_case" in
			fsmonitor) /usr/bin/git -C "$fixture_root" config --unset-all core.fsmonitor ;;
			assume-unchanged) /usr/bin/git -C "$fixture_root" update-index --no-assume-unchanged tools/mem_leak_checker_scenarios/task-8.json ;;
			skip-worktree) /usr/bin/git -C "$fixture_root" update-index --no-skip-worktree tools/mem_leak_checker_scenarios/task-8.json ;;
			replace-ref) /usr/bin/git -C "$fixture_root" update-ref -d "refs/replace/$receiving_sha" ;;
		esac
		done
	for admin_case in local-config fsmonitor assume-unchanged skip-worktree replace-ref; do
		reject "git-admin-post-$admin_case" env \
			MLC_TASK8_GIT_ADMIN_INJECTION="$admin_case" $happy_command
		! grep -q PASS "$fixture_parent/git-admin-post-$admin_case.out"
		case "$admin_case" in
			local-config)
				grep -q 'local config changed after scenario' \
					"$fixture_parent/git-admin-post-$admin_case.out"
				/usr/bin/git -C "$fixture_root" config --unset-all task8.postscenario
				;;
			fsmonitor)
				grep -q 'behavior-changing config rejected: core.fsmonitor' \
					"$fixture_parent/git-admin-post-$admin_case.out"
				/usr/bin/git -C "$fixture_root" config --unset-all core.fsmonitor
				;;
			assume-unchanged)
				grep -q 'receiving index flags rejected' \
					"$fixture_parent/git-admin-post-$admin_case.out"
				/usr/bin/git -C "$fixture_root" update-index --no-assume-unchanged \
					tools/mem_leak_checker_scenarios/task-8.json
				;;
			skip-worktree)
				grep -q 'receiving index flags rejected' \
					"$fixture_parent/git-admin-post-$admin_case.out"
				/usr/bin/git -C "$fixture_root" update-index --no-skip-worktree \
					tools/mem_leak_checker_scenarios/task-8.json
				;;
			replace-ref)
				if ! grep -q 'receiving replace refs rejected' \
					"$fixture_parent/git-admin-post-$admin_case.out"; then
					cat "$fixture_parent/git-admin-post-$admin_case.out" >&2
					exit 1
				fi
				/usr/bin/git -C "$fixture_root" update-ref -d "refs/replace/$receiving_sha"
				;;
		esac
	done
	dispatcher="$fixture_root/tools/mem_leak_checker_qa.sh"
	cp "$dispatcher" "$fixture_parent/mem_leak_checker_qa.sh"
	/usr/bin/git -C "$fixture_root" update-index --assume-unchanged \
		tools/mem_leak_checker_qa.sh
	printf '%s\n' '# hostile assume-unchanged dispatcher mutation' >>"$dispatcher"
	reject bootstrap-assume-unchanged-dispatcher /bin/bash \
		"$fixture_root/tools/mem_leak_checker_task8_qa.sh" seal-authoritative HEAD
	grep -q 'bootstrap index flags rejected' \
		"$fixture_parent/bootstrap-assume-unchanged-dispatcher.out"
	/usr/bin/git -C "$fixture_root" update-index --no-assume-unchanged \
		tools/mem_leak_checker_qa.sh
	cp "$fixture_parent/mem_leak_checker_qa.sh" "$dispatcher"
	printf '%s\n' \
		'MLC_TASK8_NEGATIVE status=PASS cases=malformed,stale,dirty,flaky,misleading,interruption,direct-red' \
		>"$fixture_parent/negative.expected"
	if ! (cd "$fixture_root" && \
		MLC_TASK8_PLAN_FIXTURE="$fixture_root/.omo/plans/mem-leak-checker-hardening.md" \
		"$fixture_root/tools/mem_leak_checker_qa.sh" receipt-negative --task 8) \
		>"$fixture_parent/negative-clean.out" 2>"$fixture_parent/negative-clean.err"; then
		cat "$fixture_parent/negative-clean.err" >&2
		exit 1
	fi
	cmp "$fixture_parent/negative.expected" "$fixture_parent/negative-clean.out"
	if [ -s "$fixture_parent/negative-clean.err" ]; then
		cat "$fixture_parent/negative-clean.err" >&2
		exit 1
	fi
	for archive_fault in missing corrupt symlink; do
		reject "archive-$archive_fault" env MLC_TASK8_ARCHIVE_FAULT="$archive_fault" $happy_command
		! grep -q PASS "$fixture_parent/archive-$archive_fault.out"
	done
	forged_parent=$(mktemp -d /tmp/mlc-task8-materialized.forge.XXXXXX)
	forged_root="$forged_parent/repository"
	source_worktree_git_dir=$(/usr/bin/git -C "$fixture_root" rev-parse --absolute-git-dir)
	source_object_dir=$(realpath "$source_worktree_git_dir/objects")
	/usr/bin/git clone -q --shared "$fixture_root" "$forged_root"
	printf '%s\n' "$source_object_dir" >"$forged_parent/alternates.expected"
	printf '%s\n' "$source_object_dir" >"$forged_root/.git/objects/info/alternates"
	cmp "$forged_parent/alternates.expected" "$forged_root/.git/objects/info/alternates"
	MLC_TASK8_MATERIALIZED=1 MLC_TASK8_SOURCE_SHA="$receiving_sha" \
		MLC_TASK8_SOURCE_WORKTREE_GIT_DIR="$source_worktree_git_dir" \
		MLC_TASK8_SOURCE_OBJECT_DIR="$source_object_dir" \
		MLC_TASK8_ALTERNATES_EXPECTED="$forged_parent/alternates.expected" \
		"$forged_root/tools/mem_leak_checker_task8_qa.sh" qemu \
		--selector fixtures --fixtures mlc_task_roots,mlc_direct_wrapper_roots \
		--repeat 1 --post-commit >"$fixture_parent/materialized-zero-local.out"
	cmp "$fixture_parent/happy-clean.out" "$fixture_parent/materialized-zero-local.out"
	/usr/bin/git -C "$forged_root" config user.name task8-forge-test
	/usr/bin/git -C "$forged_root" config user.email task8-forge-test@example.invalid
	/usr/bin/git -C "$forged_root" commit -q --allow-empty -m hostile-unreachable
	/usr/bin/git -C "$forged_root" reset -q --hard "$receiving_sha"
	reject materialized-local-object env MLC_TASK8_MATERIALIZED=1 \
		MLC_TASK8_SOURCE_SHA="$receiving_sha" \
		MLC_TASK8_SOURCE_WORKTREE_GIT_DIR="$source_worktree_git_dir" \
		MLC_TASK8_SOURCE_OBJECT_DIR="$source_object_dir" \
		MLC_TASK8_ALTERNATES_EXPECTED="$forged_parent/alternates.expected" \
		"$forged_root/tools/mem_leak_checker_task8_qa.sh" qemu \
		--selector fixtures --fixtures mlc_task_roots,mlc_direct_wrapper_roots \
		--repeat 1 --post-commit
	/usr/bin/git clone -q --bare "$fixture_root" "$forged_parent/mismatch.git"
	/usr/bin/git --git-dir="$forged_parent/mismatch.git" update-ref HEAD "$receiving_sha^"
	find "$forged_root" -depth -delete
	/usr/bin/git clone -q --shared "$fixture_root" "$forged_root"
	printf '%s\n' "$source_object_dir" >"$forged_root/.git/objects/info/alternates"
	cmp "$forged_parent/alternates.expected" "$forged_root/.git/objects/info/alternates"
	reject source-gitdir-head-mismatch env MLC_TASK8_MATERIALIZED=1 \
		MLC_TASK8_SOURCE_SHA="$receiving_sha" \
		MLC_TASK8_SOURCE_WORKTREE_GIT_DIR="$forged_parent/mismatch.git" \
		MLC_TASK8_SOURCE_OBJECT_DIR="$source_object_dir" \
		MLC_TASK8_ALTERNATES_EXPECTED="$forged_parent/alternates.expected" \
		"$forged_root/tools/mem_leak_checker_task8_qa.sh" qemu \
		--selector fixtures --fixtures mlc_task_roots,mlc_direct_wrapper_roots \
		--repeat 1 --post-commit
	find "$forged_parent" -depth -delete
	forged_parent=
	mkdir -p "$fixture_root/tools/__pycache__"
	ln -s hostile-chip "$fixture_root/os/arch/arm/include/chip"
	ln -s hostile-board "$fixture_root/os/arch/arm/include/board"
	printf '%s\n' '#error hostile generated config' >"$fixture_root/os/include/tinyara/config.h"
	hostile_pyc_relative=$(/opt/homebrew/bin/python3 -I -B - <<'PY'
import importlib.util
print(importlib.util.cache_from_source("tools/mem_leak_checker_qa_core.py"))
PY
)
	/opt/homebrew/bin/python3 -I -B - "$fixture_root" "$fixture_parent" "$hostile_pyc_relative" <<'PY'
import os, pathlib, py_compile, sys
root, temporary = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
source = root / "tools/mem_leak_checker_qa_core.py"
cache = root / sys.argv[3]
marker = temporary / "hostile-pyc-executed"
payload = ("from pathlib import Path\n" +
           f"Path({str(marker)!r}).write_text('executed\\n')\n" +
           "raise RuntimeError('hostile cached bytecode executed')\n")
size = source.stat().st_size
if len(payload.encode()) > size:
    raise SystemExit("hostile pyc payload exceeds source size")
payload += "#" * (size - len(payload.encode()))
malicious = temporary / "mem_leak_checker_task8_authoritative.py"
malicious.write_text(payload)
source_stat = source.stat()
os.utime(malicious, ns=(source_stat.st_atime_ns, source_stat.st_mtime_ns))
py_compile.compile(str(malicious), cfile=str(cache), doraise=True)
malicious.unlink()
PY
	env PYTHONDONTWRITEBYTECODE=1 /opt/homebrew/bin/python3 -I -B -c \
		"import sys; sys.path.insert(0, '$fixture_root/tools'); import mem_leak_checker_task8_authoritative"
	[ ! -e "$fixture_parent/hostile-pyc-executed" ]
	hostile_before=$(/opt/homebrew/bin/python3 -I -B - "$fixture_root" "$hostile_pyc_relative" <<'PY'
import hashlib, json, os, pathlib, stat, sys
root = pathlib.Path(sys.argv[1])
paths = ("os/arch/arm/include/chip", "os/arch/arm/include/board",
	         "os/include/tinyara/config.h", sys.argv[2])
records = []
for relative in paths:
    path = root / relative
    value = os.lstat(path)
    kind = "symlink" if stat.S_ISLNK(value.st_mode) else "file"
    records.append({"path": relative, "type": kind, "mode": stat.S_IMODE(value.st_mode),
                    "target": os.readlink(path) if kind == "symlink" else None,
                    "sha256": None if kind == "symlink" else hashlib.sha256(path.read_bytes()).hexdigest()})
print(json.dumps(records, sort_keys=True, separators=(",", ":")))
PY
)
	(cd "$fixture_root" && $happy_command) >"$fixture_parent/happy-hostile.out" 2>&1
	(cd "$fixture_root" && $failure_command) >"$fixture_parent/failure-hostile.out" 2>&1
	cmp "$fixture_parent/happy-clean.out" "$fixture_parent/happy-hostile.out"
	cmp "$fixture_parent/failure-clean.out" "$fixture_parent/failure-hostile.out"
	(cd "$fixture_root" && \
		MLC_TASK8_PLAN_FIXTURE="$fixture_root/.omo/plans/mem-leak-checker-hardening.md" \
		"$fixture_root/tools/mem_leak_checker_qa.sh" receipt-negative --task 8) \
		>"$fixture_parent/negative-hostile.out" 2>"$fixture_parent/negative-hostile.err"
	cmp "$fixture_parent/negative-clean.out" "$fixture_parent/negative-hostile.out"
	[ ! -s "$fixture_parent/negative-hostile.err" ]
	hostile_after=$(/opt/homebrew/bin/python3 -I -B - "$fixture_root" "$hostile_pyc_relative" <<'PY'
import hashlib, json, os, pathlib, stat, sys
root = pathlib.Path(sys.argv[1])
paths = ("os/arch/arm/include/chip", "os/arch/arm/include/board",
	         "os/include/tinyara/config.h", sys.argv[2])
records = []
for relative in paths:
    path = root / relative
    value = os.lstat(path)
    kind = "symlink" if stat.S_ISLNK(value.st_mode) else "file"
    records.append({"path": relative, "type": kind, "mode": stat.S_IMODE(value.st_mode),
                    "target": os.readlink(path) if kind == "symlink" else None,
                    "sha256": None if kind == "symlink" else hashlib.sha256(path.read_bytes()).hexdigest()})
print(json.dumps(records, sort_keys=True, separators=(",", ":")))
PY
)
	[ "$hostile_before" = "$hostile_after" ]
	[ ! -e "$fixture_parent/hostile-pyc-executed" ]
	find "$fixture_root/os/arch/arm/include/chip" \
		"$fixture_root/os/arch/arm/include/board" \
		"$fixture_root/os/include/tinyara/config.h" \
		"$fixture_root/$hostile_pyc_relative" -delete
	rmdir "$fixture_root/tools/__pycache__"
fi

seal="$fixture_root/tools/mem_leak_checker_qa.sh seal-task --task 8 --source HEAD"
receipt="$fixture_root/.omo/start-work/artifacts/task-8-executor/task-8-post-integration-$receiving_sha.json"
marker="$fixture_parent/survivor-fsync.marker"
cp "$fixture_root/tools/mem_leak_checker_scenarios/task-8.json" "$fixture_parent/task-8.json"
printf 'dirty\n' >>"$fixture_root/tools/mem_leak_checker_scenarios/task-8.json"
reject dirty $seal
cp "$fixture_parent/task-8.json" "$fixture_root/tools/mem_leak_checker_scenarios/task-8.json"
$seal >"$fixture_parent/first.out"
first_sha=$(shasum -a 256 "$receipt" | awk '{print $1}')
/usr/bin/git -C "$fixture_root" update-ref "refs/replace/$receiving_sha" "$receiving_sha^"
reject seal-replace-ref $seal
! grep -q PASS "$fixture_parent/seal-replace-ref.out"
/usr/bin/git -C "$fixture_root" update-ref -d "refs/replace/$receiving_sha"
hostile_bin="$fixture_parent/hostile-bin"
hostile_python="$fixture_parent/hostile-python"
mkdir "$hostile_bin" "$hostile_python"
printf '#!/bin/sh\nprintf hostile-git >%s\nexit 97\n' \
	"$fixture_parent/hostile-git.marker" >"$hostile_bin/git"
printf '#!/bin/sh\nprintf hostile-python >%s\nexit 98\n' \
	"$fixture_parent/hostile-python.marker" >"$hostile_bin/python3"
chmod +x "$hostile_bin/git" "$hostile_bin/python3"
printf 'printf hostile-bash-env >%s\n' "$fixture_parent/hostile-bash-env.marker" \
	>"$fixture_parent/hostile-bash-env"
printf '%s\n' 'raise RuntimeError("hostile PYTHONPATH executed")' > \
	"$hostile_python/sitecustomize.py"
env PATH="$hostile_bin:/usr/bin:/bin" ENV="$fixture_parent/hostile-env" \
	GIT_DIR="$fixture_parent/hostile-git-dir" GIT_WORK_TREE="$fixture_parent/hostile-work-tree" \
	PYTHONPATH="$hostile_python" PYTHONHOME="$fixture_parent/hostile-home" \
	MLC_TASK8_CHILD_BASH_ENV="$fixture_parent/hostile-bash-env" \
	MLC_TASK8_CHILD_PATH="$hostile_bin" \
	/bin/bash "$fixture_root/tools/mem_leak_checker_task8_qa.sh" seal-authoritative HEAD \
	>"$fixture_parent/hostile-environment.out"
cmp "$fixture_parent/first.out" "$fixture_parent/hostile-environment.out"
for hostile_marker in "$fixture_parent"/hostile-*.marker; do
	[ ! -e "$hostile_marker" ]
done
MLC_TASK4_SURVIVOR_FSYNC_MARKER="$marker" $seal >"$fixture_parent/second.out"
grep -qx reopened_named_file_and_directory_fsynced "$marker"
cmp "$fixture_parent/first.out" "$fixture_parent/second.out"
[ "$first_sha" = "$(shasum -a 256 "$receipt" | awk '{print $1}')" ]
MLC_TASK8_MATERIALIZED=1 MLC_TASK8_SOURCE_SHA=0000000000000000000000000000000000000000 \
	MLC_TASK8_SNAPSHOT_SHA256=0000000000000000000000000000000000000000000000000000000000000000 \
	MLC_TASK8_SNAPSHOT_NONCE=forged MLC_TASK8_SNAPSHOT_FD=999 \
	MLC_TASK8_INTERRUPT_AT_START=1 \
	MLC_TASK8_ARCHIVE_FAULT=corrupt \
	$seal >"$fixture_parent/ambient-controls.out"
cmp "$fixture_parent/first.out" "$fixture_parent/ambient-controls.out"
/opt/homebrew/bin/python3 -I -B - "$receipt" "$receiving_sha" "$receiving_tree" <<'PY'
import json, pathlib, sys
value = json.loads(pathlib.Path(sys.argv[1]).read_bytes())
assert value["schema"] == 2 and value["task"] == 8
assert value["receiving_sha"] == sys.argv[2] and value["receiving_tree"] == sys.argv[3]
assert set(value["scenario_commands"]) == {"red", "happy", "failure"}
assert [item["exit"] for item in value["scenario_exits"]] == [86, 0, 0]
assert value["qemu"] == "deferred_unexecuted_baseline_link_failure"
assert value["threat_model"] == {
    "active_same_uid_race": "excluded",
    "checked_inputs": "postvalidated",
    "trusted_initial_root": "tools/mem_leak_checker_task8_qa.sh_body_before_python_bootstrap_cannot_self_authenticate",
    "shell_startup_before_script_body": "excluded",
    "scope": "cooperative_local_qa",
    "trusted_entrypoint_bootstrap": "required_before_validation",
}
PY

cp "$receipt" "$fixture_parent/canonical.json"
for mutation in stale tree surplus omission content scenario plan commands exits publication \
	threat-omission threat-surplus threat-type threat-active-same-uid-race \
	threat-checked-inputs threat-trusted-initial-root threat-shell-startup \
	threat-scope threat-bootstrap; do
	/opt/homebrew/bin/python3 -I -B - "$fixture_parent/canonical.json" "$receipt" "$mutation" <<'PY'
import json, pathlib, sys
source, target, mutation = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
value = json.loads(source.read_bytes())
if mutation == "stale": value["receiving_sha"] = "0" * 40
elif mutation == "tree": value["receiving_tree"] = "0" * 40
elif mutation == "surplus": value["surplus"] = True
elif mutation == "omission": value.pop("scenario_exits")
elif mutation == "content": value["content_sha256"] = "0" * 64
elif mutation == "scenario": value["scenario_sha256"] = "0" * 64
elif mutation == "plan": value["normalized_plan_sha256"] = "0" * 64
elif mutation == "commands": value["scenario_commands"]["happy"] = ["false"]
elif mutation == "exits": value["scenario_exits"][0]["exit"] = 0
elif mutation == "publication": value["publication"]["external_writer_authenticated"] = True
elif mutation == "threat-omission": value.pop("threat_model")
elif mutation == "threat-surplus": value["threat_model"]["surplus"] = "excluded"
elif mutation == "threat-type": value["threat_model"] = []
elif mutation == "threat-active-same-uid-race": value["threat_model"]["active_same_uid_race"] = "included"
elif mutation == "threat-checked-inputs": value["threat_model"]["checked_inputs"] = "prevalidated"
elif mutation == "threat-trusted-initial-root": value["threat_model"]["trusted_initial_root"] = "self_authenticated"
elif mutation == "threat-shell-startup": value["threat_model"]["shell_startup_before_script_body"] = "included"
elif mutation == "threat-scope": value["threat_model"]["scope"] = "hostile_same_uid"
else: value["threat_model"]["trusted_entrypoint_bootstrap"] = "optional"
target.write_text(json.dumps(value) + "\n")
PY
	reject "$mutation" $seal
	cp "$fixture_parent/canonical.json" "$receipt"
done

reject replay-fsync env MLC_TASK4_IDENTICAL_FSYNC_FAILURE=1 $seal
[ "$first_sha" = "$(shasum -a 256 "$receipt" | awk '{print $1}')" ]
for injection in surplus cross duplicate missing blank stderr unterminated exit; do
	reject "transcript-$injection" env MLC_TASK8_TRANSCRIPT_INJECTION="$injection" $seal
	diff -q "$fixture_parent/canonical.json" "$receipt" >/dev/null
done
reject transcript-unknown env MLC_TASK8_TRANSCRIPT_INJECTION=unknown $seal
diff -q "$fixture_parent/canonical.json" "$receipt" >/dev/null

reject post-use-untracked env MLC_TASK8_POST_USE_INJECTION=receiving-untracked $seal
diff -q "$fixture_parent/canonical.json" "$receipt" >/dev/null
find "$fixture_root/task8-unexpected-post-use" -delete
/usr/bin/git init -q --bare "$fixture_parent/alternate-store.git"
reject post-use-alternates env MLC_TASK8_POST_USE_INJECTION=alternates \
	MLC_TASK8_ALTERNATE_INJECTION_DIR="$fixture_parent/alternate-store.git/objects" $seal
diff -q "$fixture_parent/canonical.json" "$receipt" >/dev/null

find "$receipt" -delete
mkfifo "$receipt"
reject fifo $seal
find "$receipt" -delete
ln -s "$fixture_parent/canonical.json" "$receipt"
reject symlink $seal
find "$receipt" -delete

(trap - EXIT HUP INT TERM; $seal >"$fixture_parent/concurrent-a.out") & first=$!
(trap - EXIT HUP INT TERM; $seal >"$fixture_parent/concurrent-b.out") & second=$!
wait "$first"
wait "$second"
[ "$first_sha" = "$(shasum -a 256 "$receipt" | awk '{print $1}')" ]
printf '%s\n' 'MLC_TASK8_SEAL_STATE status=PASS double_seal=byte-identical cases=dirty,stale,tree,surplus,omission,content,scenario,plan,commands,exits,publication,threat-omission,threat-surplus,threat-type,threat-active-same-uid-race,threat-checked-inputs,threat-trusted-initial-root,threat-shell-startup,threat-scope,threat-bootstrap,replay-fsync,transcript-surplus,transcript-cross,transcript-duplicate,transcript-missing,transcript-blank,transcript-stderr,transcript-unterminated,transcript-exit,transcript-unknown,post-use-untracked,post-use-alternates,fifo,symlink,concurrency,archive-missing,archive-corrupt,archive-symlink,linked-worktree,worktree-gitdir-as-object-dir,materialized-zero-local,materialized-local-unreachable,source-gitdir-head-mismatch,materialized-required-headers,git-shared-remote-branch-context,git-fsmonitor,git-assume-unchanged,git-skip-worktree,git-replace-ref,git-admin-post-local-config,git-admin-post-fsmonitor,git-admin-post-assume-unchanged,git-admin-post-skip-worktree,git-admin-post-replace-ref,bootstrap-assume-unchanged-dispatcher,seal-replace-ref,ambient-controls,hostile-environment,signals-hup-int-term,receipt-negative-exact,receipt-private-dirty,receipt-inode survivor_fsync=true red_linkage=true tracked_execution_snapshot=true hostile_source_unchanged=true hostile_routes=happy,failure,receipt-negative hostile_pyc_side_effect_guarded=true'
