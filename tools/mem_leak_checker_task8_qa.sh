#!/usr/bin/env bash
set -euo pipefail

trusted_path=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
PATH=$trusted_path
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
bash_bin=/opt/homebrew/bin/bash
model=
arm_m=
arm_a=
stub=
mkconfig=
production_dir=
materialized_parent=
negative_parent=
variant_roots=()
cleanup_done=0
cleanup() {
	cleanup_status=$?
	trap - EXIT HUP INT TERM
	[ "$cleanup_done" = 0 ] || exit "$cleanup_status"
	cleanup_done=1
	rm -rf "$model" "$arm_m" "$arm_a" "$stub" "$mkconfig" "$production_dir"
	for variant_root in ${variant_roots[@]+"${variant_roots[@]}"}; do
		case "$variant_root" in
		/tmp/mlc-task8-object.*|/private/tmp/mlc-task8-object.*)
			chmod -R u+w "$variant_root" 2>/dev/null || true
			rm -rf "$variant_root"
			;;
		esac
	done
	case "$materialized_parent" in
		/tmp/mlc-task8-materialized.*|/private/tmp/mlc-task8-materialized.*)
			chmod -R u+w "$materialized_parent" 2>/dev/null || true
			rm -rf "$materialized_parent"
			;;
		"") ;;
		*) cleanup_status=1 ;;
	esac
	case "$negative_parent" in
		/tmp/mlc-task8-negative.*|/private/tmp/mlc-task8-negative.*)
			chmod -R u+w "$negative_parent" 2>/dev/null || true
			rm -rf "$negative_parent"
			;;
		"") ;;
		*) cleanup_status=1 ;;
	esac
	exit "$cleanup_status"
}
trap cleanup EXIT
trap 'trap - HUP INT TERM; exit 129' HUP
trap 'trap - HUP INT TERM; exit 130' INT
trap 'trap - HUP INT TERM; exit 143' TERM

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mode=${1:?usage: mem_leak_checker_task8_qa.sh red|qemu|negative|signal-probe|seal-authoritative SOURCE}
shift
artifact_dir=${MLC_TASK8_ARTIFACT_DIR:-/tmp/mlc-task8-development}
mkdir -p "$artifact_dir"
model=$(mktemp /tmp/mlc-task8-model.XXXXXX)
arm_m=$(mktemp /tmp/mlc-task8-armv7m.XXXXXX)
arm_a=$(mktemp /tmp/mlc-task8-armv7a.XXXXXX)
stub=$(mktemp -d /tmp/mlc-task8-stub.XXXXXX)
mkconfig=$(mktemp /tmp/mlc-task8-mkconfig.XXXXXX)
production_dir=$(mktemp -d /tmp/mlc-task8-production.XXXXXX)

scenario="$root/tools/mem_leak_checker_scenarios/task-8.json"

value_after() {
	name=$1
	shift
	while [ "$#" -gt 0 ]; do
		if [ "$1" = "$name" ]; then
			[ "$#" -ge 2 ] || exit 64
			printf '%s\n' "$2"
			return
		fi
		shift
	done
	exit 64
}

inject_transcript() {
	case "$root" in
		/private/tmp/mlc-task5-seal-test.*/*|/tmp/mlc-task5-seal-test.*/*) ;;
		*) [ -z "${MLC_TASK8_TRANSCRIPT_INJECTION:-}" ] || exit 64; return ;;
	esac
	case "${MLC_TASK8_TRANSCRIPT_INJECTION:-}" in
		"") ;;
		surplus) printf '%s\n' 'MLC_TASK8_SURPLUS status=PASS' ;;
		blank) printf '\n' ;;
		cross) printf '%s\n' 'MLC_TASK8_FAILURES model_status=PASS expected_incomplete=TASK_CONTEXT expected_rows=0 mutations=migration,tcb,irq,mode,sp,mask,cpsr,xpsr reuse=valid_after_rejection' ;;
		duplicate) printf 'MLC_TASK8_ROUTE kind=%s selector=%s fixtures=%s repeat=1 post_commit=true\n' "$route" "$selector" "$fixtures" ;;
		missing) ;;
		stderr) printf '%s\n' 'MLC_TASK8_INJECTED_STDERR' >&2 ;;
		unterminated) printf '%s' 'MLC_TASK8_UNTERMINATED' ;;
		exit) return 87 ;;
		*) exit 64 ;;
	esac
}

validate_object_store() {
	object_root=$1
	alternates_expected=$2
	[ -d "$object_root" ] && [ ! -L "$object_root" ]
	[ -d "$object_root/info" ] && [ ! -L "$object_root/info" ]
	[ -d "$object_root/pack" ] && [ ! -L "$object_root/pack" ]
	[ -f "$object_root/info/alternates" ] && [ ! -L "$object_root/info/alternates" ]
	cmp "$alternates_expected" "$object_root/info/alternates"
	object_entries=$(/usr/bin/find "$object_root" -mindepth 1 -print | \
		/usr/bin/sed "s|^$object_root/||" | LC_ALL=C /usr/bin/sort)
	[ "$object_entries" = "info
info/alternates
pack" ]
}

validate_receiving_git_admin() {
	[ -z "$($git_bin -C "$root" for-each-ref --format='%(refname)' refs/replace)" ] || {
		printf '%s\n' 'Task8 receiving replace refs rejected' >&2
		return 1
	}
	while IFS= read -r config_name; do
		case "$config_name" in
			core.fsmonitor|core.fsmonitorhookversion|core.hookspath|core.worktree|\
			core.sparsecheckout|core.sparsecheckoutcone|core.attributesfile|\
			core.excludesfile|diff.external|filter.*.clean|filter.*.smudge|\
			filter.*.process)
				printf 'Task8 receiving behavior-changing config rejected: %s\n' \
					"$config_name" >&2
				return 1
				;;
		esac
	done < <($git_bin -C "$root" config --local --name-only --list)
	if $git_bin -C "$root" ls-files -v | \
		awk 'substr($0, 1, 1) == "S" || substr($0, 1, 1) ~ /[a-z]/ { print; exit }' | \
		grep -q .; then
		printf '%s\n' 'Task8 receiving index flags rejected' >&2
		return 1
	fi
	$git_bin -C "$root" diff --quiet -- || {
		printf '%s\n' 'Task8 receiving worktree content rejected' >&2
		return 1
	}
	$git_bin -C "$root" diff --cached --quiet -- || {
		printf '%s\n' 'Task8 receiving index content rejected' >&2
		return 1
	}
}

capture_receiving_git_admin() {
	snapshot_dir=$1
	mkdir -p "$snapshot_dir"
	$git_bin -C "$root" config --local --null --list >"$snapshot_dir/local-config"
	$git_bin -C "$root" for-each-ref --format='%(refname)%00%(objectname)' \
		refs/replace >"$snapshot_dir/replace-refs"
	$git_bin -C "$root" ls-files -v >"$snapshot_dir/index-flags"
}

case "$mode" in
signal-probe)
	[ "$#" -eq 2 ] || exit 64
	case "$1" in HUP|INT|TERM) signal_name=$1 ;; *) exit 64 ;; esac
	case "$2" in
		/private/tmp/mlc-task5-seal-test.*/*|/tmp/mlc-task5-seal-test.*/*) ;;
		*) exit 64 ;;
	esac
	printf '%s\n' "$model" "$arm_m" "$arm_a" "$stub" "$mkconfig" \
		"$production_dir" >"$2"
	kill -s "$signal_name" "$$"
	exit 1
	;;
red)
	[ "${MLC_TASK8_RED_ARMED:-0}" = 1 ] || {
		printf '%s\n' 'Task8 RED requires the canonical runner' >&2
		exit 1
	}
	$python_bin -I -B - "$root" "$scenario" "$artifact_dir/task-8-red.json" <<'PY'
import hashlib, json, pathlib, subprocess, sys
root, scenario_path, receipt_path = map(pathlib.Path, sys.argv[1:])
scenario = json.loads(scenario_path.read_text())
red = scenario["red"][0]
paths = [
    "tools/mem_leak_checker_scenarios/task-8.json",
    "tools/mem_leak_checker_task8_qa.sh",
    "tools/tests/mem_leak_checker_task_roots_model.c",
]
files = [{"path": path, "sha256": hashlib.sha256((root / path).read_bytes()).hexdigest()} for path in paths]
receipt = {
    "schema": 1, "task": 8, "kind": "development-red",
    "baseline_sha": subprocess.check_output(["/usr/bin/git", "-C", root, "rev-parse", "HEAD"], text=True).strip(),
    "staged_write_tree": subprocess.check_output(["/usr/bin/git", "-C", root, "write-tree"], text=True).strip(),
    "fixture_files": files,
    "fixture_digest": hashlib.sha256("".join(f"{item['sha256']}  {item['path']}\n" for item in files).encode()).hexdigest(),
    "fixture_patch_sha256": hashlib.sha256(subprocess.check_output(["/usr/bin/git", "-C", root, "diff", "--cached", "--binary", "HEAD", "--", *paths])).hexdigest(),
    "command": red["command"], "exit": 86,
}
receipt_path.parent.mkdir(parents=True, exist_ok=True)
receipt_path.write_text(json.dumps(receipt, sort_keys=True, separators=(",", ":")) + "\n")
print("MLC_TASK8_RED status=expected_failure exit=86 evidence=development_only authoritative=false")
PY
	exit 86
	;;
qemu)
	[ "$#" -eq 7 ] || exit 64
	selector=$(value_after --selector "$@")
	fixtures=$(value_after --fixtures "$@")
	repeat=$(value_after --repeat "$@")
	[ "$repeat" = 1 ] || exit 64
	[ "${7:-}" = --post-commit ] || exit 64
	case "$selector:$fixtures" in
		fixtures:mlc_task_roots,mlc_direct_wrapper_roots) route=happy; fixture_arg=mlc_task_roots ;;
		fixture:mlc_invalid_task_irq_context) route=failure; fixture_arg=mlc_invalid_task_irq_context ;;
		*) exit 64 ;;
	esac
	if [ "${MLC_TASK8_MATERIALIZED:-0}" != 1 ]; then
		validate_receiving_git_admin
		source_sha=$($git_bin -C "$root" rev-parse HEAD)
		source_worktree_git_dir=$($git_bin -C "$root" rev-parse --absolute-git-dir)
		source_common_dir=$($git_bin -C "$root" rev-parse --git-common-dir)
		case "$source_common_dir" in
			/*) ;;
			*) source_common_dir="$root/$source_common_dir" ;;
		esac
		source_object_dir=$(realpath "$source_common_dir/objects")
		[ "$($git_bin --git-dir="$source_worktree_git_dir" rev-parse HEAD)" = "$source_sha" ]
		case "${MLC_TASK8_ARCHIVE_FAULT:-}" in
			"") ;;
			missing|corrupt|symlink)
				case "$root" in
					/private/tmp/mlc-task5-seal-test.*/*|/tmp/mlc-task5-seal-test.*/*) ;;
					*) exit 64 ;;
				esac
				;;
			*) exit 64 ;;
		esac
		materialized_parent=$(mktemp -d /tmp/mlc-task8-materialized.XXXXXX)
		materialized_root="$materialized_parent/repository"
			$git_bin clone -q --shared --no-checkout "$root" "$materialized_root"
			printf '%s\n' "$source_object_dir" >"$materialized_parent/alternates.expected"
			validate_object_store "$materialized_root/.git/objects" \
				"$materialized_parent/alternates.expected"
			capture_receiving_git_admin "$materialized_parent/receiving-admin-before"
		$git_bin -C "$materialized_root" sparse-checkout init --no-cone
		$git_bin -C "$materialized_root" sparse-checkout set --no-cone \
			build/configs/qemu/tc_1m/defconfig \
			build/configs/rtl8730e/flat_apps/defconfig \
			os/arch/arm/include os/arch/arm/src/amebasmart/Make.defs \
			os/arch/arm/src/armv7-a/arm_mem_leak_capture.S \
			os/arch/arm/src/armv7-m/arm_mem_leak_capture.S \
			os/arch/arm/src/tiva/Make.defs os/include \
			os/kernel/binary_manager os/kernel/debug/mem_leak_checker.c \
			os/kernel/debug/mem_leak_checker_candidates.h \
			os/kernel/debug/mem_leak_checker_candidates_internal.h \
			os/kernel/debug/mem_leak_checker_core.h \
			os/kernel/debug/mem_leak_checker_domain.c \
			os/kernel/debug/mem_leak_checker_domain.h \
			os/kernel/debug/mem_leak_checker_lifecycle.c \
			os/kernel/debug/mem_leak_checker_lifecycle.h \
			os/kernel/debug/mem_leak_checker_pause.h \
			os/kernel/debug/mem_leak_checker_pause_owner.h \
			os/kernel/debug/mem_leak_checker_roots.c \
			os/kernel/debug/mem_leak_checker_roots.h \
			os/kernel/debug/mem_leak_checker_roots_test.c \
			os/kernel/sched os/kernel/task os/tools \
			tools/mem_leak_checker_qa.sh tools/mem_leak_checker_task8_qa.sh \
			tools/tests/mem_leak_checker_production_roots_fixture.c \
			tools/tests/mem_leak_checker_stubs \
			tools/tests/mem_leak_checker_task_roots_model.c
		checkout_sha=$source_sha
		[ "${MLC_TASK8_ARCHIVE_FAULT:-}" != missing ] || \
			checkout_sha=0000000000000000000000000000000000000000
		$git_bin -C "$materialized_root" checkout -q --detach "$checkout_sha"
		case "${MLC_TASK8_ARCHIVE_FAULT:-}" in
			corrupt) printf '#!/usr/bin/env bash\nthis is (\n' >"$materialized_root/tools/mem_leak_checker_task8_qa.sh" ;;
			symlink)
				find "$materialized_root/tools/mem_leak_checker_task8_qa.sh" -delete
				ln -s ../escape "$materialized_root/tools/mem_leak_checker_task8_qa.sh"
				;;
		esac
		find "$materialized_root" -path "$materialized_root/.git" -prune -o \
			-type d -exec chmod a-w {} + -o -type f -exec chmod a-w {} +
		validate_object_store "$materialized_root/.git/objects" \
			"$materialized_parent/alternates.expected"
		[ -z "$($git_bin --git-dir="$source_worktree_git_dir" for-each-ref \
			--format='%(refname)' refs/replace)" ]
		if [ "$route" = happy ]; then
			materialized_options=(--fixtures mlc_task_roots,mlc_direct_wrapper_roots)
		else
			materialized_options=(--fixture mlc_invalid_task_irq_context)
		fi
		set +e
		MLC_TASK8_MATERIALIZED=1 MLC_TASK8_SOURCE_SHA="$source_sha" \
			MLC_TASK8_SOURCE_WORKTREE_GIT_DIR="$source_worktree_git_dir" \
			MLC_TASK8_SOURCE_OBJECT_DIR="$source_object_dir" \
			MLC_TASK8_ALTERNATES_EXPECTED="$materialized_parent/alternates.expected" \
			MLC_TASK8_ARTIFACT_DIR="$artifact_dir" \
			"$materialized_root/tools/mem_leak_checker_qa.sh" qemu --task 8 \
			"${materialized_options[@]}" --repeat 1 --post-commit \
			>"$materialized_parent/transcript.log" 2>&1
			materialized_exit=$?
			set -e
			case "${MLC_TASK8_GIT_ADMIN_INJECTION:-}" in
				"") ;;
				local-config|fsmonitor|assume-unchanged|skip-worktree|replace-ref)
					case "$root" in
						/private/tmp/mlc-task5-seal-test.*/*|/tmp/mlc-task5-seal-test.*/*) ;;
						*) exit 64 ;;
					esac
					;;
				*) exit 64 ;;
			esac
			case "${MLC_TASK8_GIT_ADMIN_INJECTION:-}" in
				"") ;;
				local-config) $git_bin -C "$root" config task8.postscenario true ;;
				fsmonitor) $git_bin -C "$root" config core.fsmonitor true ;;
				assume-unchanged)
					$git_bin -C "$root" update-index --assume-unchanged \
						tools/mem_leak_checker_scenarios/task-8.json ;;
				skip-worktree)
					$git_bin -C "$root" update-index --skip-worktree \
						tools/mem_leak_checker_scenarios/task-8.json ;;
				replace-ref)
					$git_bin -C "$root" update-ref "refs/replace/$source_sha" "$source_sha^" ;;
			esac
		case "${MLC_TASK8_POST_USE_INJECTION:-}" in
			"") ;;
			alternates)
				case "${MLC_TASK8_ALTERNATE_INJECTION_DIR:-}" in
					/private/tmp/mlc-task5-seal-test.*/*|/tmp/mlc-task5-seal-test.*/*) ;;
					*) exit 64 ;;
				esac
				printf '%s\n' "$MLC_TASK8_ALTERNATE_INJECTION_DIR" > \
					"$materialized_root/.git/objects/info/alternates"
				;;
			receiving-untracked) : >"$root/task8-unexpected-post-use" ;;
			*) exit 64 ;;
		esac
			validate_object_store "$materialized_root/.git/objects" \
				"$materialized_parent/alternates.expected"
			validate_receiving_git_admin
			capture_receiving_git_admin "$materialized_parent/receiving-admin-after"
			cmp -s "$materialized_parent/receiving-admin-before/local-config" \
				"$materialized_parent/receiving-admin-after/local-config" || {
				printf '%s\n' 'Task8 receiving local config changed after scenario' >&2
				exit 1
			}
			cmp -s "$materialized_parent/receiving-admin-before/replace-refs" \
				"$materialized_parent/receiving-admin-after/replace-refs" || {
				printf '%s\n' 'Task8 receiving replace refs changed after scenario' >&2
				exit 1
			}
			cmp -s "$materialized_parent/receiving-admin-before/index-flags" \
				"$materialized_parent/receiving-admin-after/index-flags" || {
				printf '%s\n' 'Task8 receiving index flags changed after scenario' >&2
				exit 1
			}
		[ "$($git_bin --git-dir="$source_worktree_git_dir" rev-parse HEAD)" = "$source_sha" ]
		[ "$($git_bin -C "$root" rev-parse HEAD)" = "$source_sha" ] || exit 1
		[ "$($git_bin -C "$materialized_root" rev-parse HEAD)" = "$source_sha" ] || exit 1
		$git_bin -C "$materialized_root" diff --quiet --
		$git_bin -C "$materialized_root" diff --cached --quiet --
		[ -z "$($git_bin -C "$materialized_root" status --porcelain --untracked-files=all)" ] || exit 1
		cat "$materialized_parent/transcript.log"
		exit "$materialized_exit"
	fi
	case "${MLC_TASK8_SOURCE_SHA:-}" in
		[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]) ;;
		*) exit 64 ;;
	esac
	case "$root" in
		/private/tmp/mlc-task8-materialized.*/*|/tmp/mlc-task8-materialized.*/*) ;;
		*) exit 64 ;;
	esac
	validate_object_store "$root/.git/objects" "$MLC_TASK8_ALTERNATES_EXPECTED"
	[ "$(cat "$MLC_TASK8_ALTERNATES_EXPECTED")" = "$MLC_TASK8_SOURCE_OBJECT_DIR" ]
	[ -z "$($git_bin --git-dir="$MLC_TASK8_SOURCE_WORKTREE_GIT_DIR" for-each-ref \
		--format='%(refname)' refs/replace)" ]
	[ "$(/usr/bin/git --git-dir="$MLC_TASK8_SOURCE_WORKTREE_GIT_DIR" rev-parse HEAD)" = \
		"$MLC_TASK8_SOURCE_SHA" ] || exit 1
	/usr/bin/git --git-dir="$MLC_TASK8_SOURCE_WORKTREE_GIT_DIR" cat-file -e \
		"$MLC_TASK8_SOURCE_SHA^{commit}"
	env GIT_OBJECT_DIRECTORY="$MLC_TASK8_SOURCE_OBJECT_DIR" \
		/usr/bin/git --git-dir="$MLC_TASK8_SOURCE_WORKTREE_GIT_DIR" cat-file -e \
		"$MLC_TASK8_SOURCE_SHA^{commit}"
	[ "$($git_bin -C "$root" rev-parse HEAD)" = "$MLC_TASK8_SOURCE_SHA" ] || exit 1
	[ "$($git_bin -C "$root" rev-parse --is-inside-work-tree)" = true ] || exit 1
	$git_bin -C "$root" diff --quiet --
	$git_bin -C "$root" diff --cached --quiet --
	[ -z "$($git_bin -C "$root" status --porcelain --untracked-files=all)" ] || exit 1
	! $git_bin -C "$root" ls-files -s | awk '$1 == "120000" { found=1 } END { exit !found }'
	[ "${MLC_TASK8_INTERRUPT_AT_START:-0}" != 1 ] || kill -TERM $$
	printf 'MLC_TASK8_ROUTE kind=%s selector=%s fixtures=%s repeat=1 post_commit=true\n' \
		"$route" "$selector" "$fixtures"
	cc -std=c11 -Wall -Wextra -Werror \
		"$root/tools/tests/mem_leak_checker_task_roots_model.c" -o "$model"
	"$model" "$fixture_arg" | tee "$artifact_dir/model.log"
	mkdir -p "$production_dir"
	production_flags=(-std=c11 -Wall -Wextra -Werror -DCONFIG_SMP=1 \
		-I"$root/tools/tests/mem_leak_checker_stubs" \
		-I"$root/os/kernel/debug")
	cc "${production_flags[@]}" -DCONFIG_ARCH_ARMV7A_FAMILY=1 \
		-Dmlc_validate_current_capture=mlc_validate_current_capture_a \
		-Dmlc_validate_saved_task_roots=mlc_validate_saved_task_roots_a \
		-Dmlc_test_run_saved_task_scan=mlc_test_run_saved_task_scan_a \
		-Dup_mem_leak_capture_identity=up_mem_leak_capture_identity_a \
		-Dup_mem_leak_capture_current=up_mem_leak_capture_current_a \
		-c "$root/os/kernel/debug/mem_leak_checker_roots.c" \
		-o "$production_dir/roots-a.o"
	cc "${production_flags[@]}" \
		-Dmlc_validate_saved_task_roots=mlc_validate_saved_task_roots_a \
		-Dmlc_test_run_saved_task_scan=mlc_test_run_saved_task_scan_a \
		-c "$root/os/kernel/debug/mem_leak_checker_roots_test.c" \
		-o "$production_dir/seam-a.o"
	cc "${production_flags[@]}" -DCONFIG_ARCH_CORTEXM3=1 \
		-Dmlc_validate_current_capture=mlc_validate_current_capture_m \
		-Dmlc_validate_saved_task_roots=mlc_validate_saved_task_roots_m \
		-Dmlc_test_run_saved_task_scan=mlc_test_run_saved_task_scan_m \
		-Dup_mem_leak_capture_identity=up_mem_leak_capture_identity_m \
		-Dup_mem_leak_capture_current=up_mem_leak_capture_current_m \
		-c "$root/os/kernel/debug/mem_leak_checker_roots.c" \
		-o "$production_dir/roots-m.o"
	cc "${production_flags[@]}" \
		-Dmlc_validate_saved_task_roots=mlc_validate_saved_task_roots_m \
		-Dmlc_test_run_saved_task_scan=mlc_test_run_saved_task_scan_m \
		-c "$root/os/kernel/debug/mem_leak_checker_roots_test.c" \
		-o "$production_dir/seam-m.o"
	cc "${production_flags[@]}" \
		-c "$root/os/kernel/debug/mem_leak_checker_lifecycle.c" \
		-o "$production_dir/lifecycle.o"
	cc "${production_flags[@]}" \
		-c "$root/os/kernel/debug/mem_leak_checker_budget.c" \
		-o "$production_dir/budget.o"
	cc "${production_flags[@]}" \
		"$root/tools/tests/mem_leak_checker_production_roots_fixture.c" \
		"$production_dir/roots-a.o" "$production_dir/roots-m.o" \
		"$production_dir/seam-a.o" "$production_dir/seam-m.o" \
		"$production_dir/lifecycle.o" "$production_dir/budget.o" -o "$production_dir/fixture"
	"$production_dir/fixture" "$fixture_arg" | tee "$artifact_dir/production-fixture.log"
	if [ "$route" = failure ]; then
		inject_transcript
		[ "${MLC_TASK8_TRANSCRIPT_INJECTION:-}" = missing ] || \
			printf '%s\n' 'MLC_TASK8_QEMU status=deferred_unexecuted_baseline_link_failure'
		exit 0
	fi
	mkdir -p "$stub/tinyara"
	: >"$stub/tinyara/config.h"
	clang --target=arm-none-eabi -mcpu=cortex-m3 -mthumb \
		-D__ASSEMBLY__ -DCONFIG_MEM_LEAK_CHECKER -I"$stub" -I"$root/os/include" \
		-c "$root/os/arch/arm/src/armv7-m/arm_mem_leak_capture.S" -o "$arm_m"
	clang --target=arm-none-eabi -mcpu=cortex-a32 -marm \
		-D__ASSEMBLY__ -DCONFIG_MEM_LEAK_CHECKER -I"$stub" -I"$root/os/include" \
		-c "$root/os/arch/arm/src/armv7-a/arm_mem_leak_capture.S" -o "$arm_a"
	test -s "$arm_m" && test -s "$arm_a"
	nm "$arm_m" | grep -Eq ' T prctl$'
	nm "$arm_m" | grep -Eq ' T run_all_mem_leak_checker$'
	nm "$arm_m" | grep -Eq ' U task_prctl_impl$'
	nm "$arm_m" | grep -Eq ' U run_all_mem_leak_checker_with_capture$'
	nm "$arm_a" | grep -Eq ' T prctl$'
	nm "$arm_a" | grep -Eq ' T run_all_mem_leak_checker$'
	nm "$arm_a" | grep -Eq ' U task_prctl_impl$'
	nm "$arm_a" | grep -Eq ' U run_all_mem_leak_checker_with_capture$'
	for optimization in O0 O1 O2 O3 Os; do
		for architecture in armv7m armv7a; do
			matrix_object="$stub/$architecture-$optimization.o"
			matrix_dump="$stub/$architecture-$optimization.dump"
			if [ "$architecture" = armv7m ]; then
				matrix_flags=(-mcpu=cortex-m3 -mthumb)
				matrix_source=armv7-m
			else
				matrix_flags=(-mcpu=cortex-a32 -marm)
				matrix_source=armv7-a
			fi
			clang --target=arm-none-eabi "-${optimization}" "${matrix_flags[@]}" \
				-D__ASSEMBLY__ -DCONFIG_MEM_LEAK_CHECKER -I"$stub" \
				-I"$root/os/include" -c \
				"$root/os/arch/arm/src/$matrix_source/arm_mem_leak_capture.S" \
				-o "$matrix_object"
			objdump -d "$matrix_object" >"$matrix_dump"
			$python_bin -I -B - "$matrix_dump" <<'PY'
import pathlib, re, sys
dump = pathlib.Path(sys.argv[1]).read_text()
section = dump.split("<run_all_mem_leak_checker>:", 1)[1]
instructions = []
for line in section.splitlines():
    match = re.match(r"\s*[0-9a-f]+:\s+(?:[0-9a-f]{2,8}\s+)+(.+)", line)
    if match:
        instructions.append(match.group(1).strip())
assert re.match(r"mov\s+r1, r0$", instructions[0])
assert instructions[1].startswith("b")
assert "push" in instructions[2]
assert instructions[3].startswith("sub") and ("#0x48" in instructions[3] or "#72" in instructions[3])
assert any("#0x80" in item or "#128" in item for item in instructions)
PY
		done
	done
	cc "$root/os/tools/mkconfig.c" "$root/os/tools/cfgdefine.c" -o "$mkconfig"
	for variant in qemu/tc_1m:armv7m:tiva rtl8730e/flat_apps:armv7a:amebasmart; do
		config=${variant%%:*}
		rest=${variant#*:}
		architecture=${rest%%:*}
		chip=${rest#*:}
		variant_root=$(mktemp -d /tmp/mlc-task8-object.XXXXXX)
		variant_roots+=("$variant_root")
		mkdir -p "$variant_root/include/tinyara" "$variant_root/include/arch"
		cp "$root/build/configs/$config/defconfig" "$variant_root/.config"
		"$mkconfig" "$variant_root" >"$variant_root/include/tinyara/config.h"
		printf '%s\n' '#undef CONFIG_HEAPINFO_USER_GROUP' >>"$variant_root/include/tinyara/config.h"
		cp -R "$root/os/arch/arm/include/." "$variant_root/include/arch/"
		rm -f "$variant_root/include/arch/chip"
		ln -s "$chip" "$variant_root/include/arch/chip"
		if [ "$architecture" = armv7m ]; then
			cpu_flags=(-mcpu=cortex-m3 -mthumb -mfloat-abi=soft)
		else
			cpu_flags=(-mcpu=cortex-a32 -marm -mfloat-abi=soft)
		fi
		for source in mem_leak_checker_roots.c mem_leak_checker.c; do
			clang --target=arm-none-eabi -c -fno-builtin -Wall -Werror \
				-Wstrict-prototypes -Wshadow -Wundef \
				-Wno-implicit-function-declaration -Wno-unused-function \
				-Wno-unused-but-set-variable -Os -fomit-frame-pointer "${cpu_flags[@]}" \
				-D__KERNEL__ -I"$root/os" -I"$root/os/kernel" \
				-I"$variant_root/include" -isystem "$root/os/include" \
				-isystem "$root/framework/include" -isystem "$root/external/include" \
				-isystem "$root/os/net/lwip/src/include" \
				"$root/os/kernel/debug/$source" \
				-o "$variant_root/${source%.c}.o"
		done
		clang --target=arm-none-eabi -c -fno-builtin -Wall -Werror \
			-Wstrict-prototypes -Wshadow -Wundef \
			-Wno-implicit-function-declaration -Wno-unused-function \
			-Wno-unused-but-set-variable -Os -fomit-frame-pointer "${cpu_flags[@]}" \
			-D__KERNEL__ -I"$root/os" -I"$root/os/kernel" \
			-I"$variant_root/include" -isystem "$root/os/include" \
			-isystem "$root/framework/include" -isystem "$root/external/include" \
			-isystem "$root/os/net/lwip/src/include" \
			"$root/os/kernel/task/task_prctl.c" \
			-o "$variant_root/task_prctl.o"
		nm "$variant_root/task_prctl.o" | grep -Eq ' T task_prctl_impl$'
		! nm "$variant_root/mem_leak_checker.o" | grep -Eq ' T run_all_mem_leak_checker$'
	done
	$python_bin -I -B - "$root" <<'PY' | tee "$artifact_dir/static.log"
from pathlib import Path
import re, sys
root = Path(sys.argv[1])
arch = (root / "os/include/tinyara/arch.h").read_text()
checker = (root / "os/kernel/debug/mem_leak_checker.c").read_text()
prctl = (root / "os/kernel/task/task_prctl.c").read_text()
roots = (root / "os/kernel/debug/mem_leak_checker_roots.c").read_text()
test_seam = (root / "os/kernel/debug/mem_leak_checker_roots_test.c").read_text()
production_fixture = (root / "tools/tests/mem_leak_checker_production_roots_fixture.c").read_text()
mm = (root / "os/include/tinyara/mm/mm.h").read_text()
tiva = (root / "os/arch/arm/src/tiva/Make.defs").read_text()
ameba = (root / "os/arch/arm/src/amebasmart/Make.defs").read_text()
assert "struct up_mem_leak_capture_s" in arch
assert re.search(r"#define\s+UP_MEM_LEAK_CAPTURE_SIZE\s+72\b", arch)
assert arch.count("void up_mem_leak_capture_current(") == 1
assert "arm_mem_leak_capture.S" in tiva and "arm_mem_leak_capture.S" in ameba
assert checker.count("int run_all_mem_leak_checker(int checker_pid)") == 1
assert checker.count("int run_all_mem_leak_checker_with_capture(") == 1
assert checker.index("#if !defined(CONFIG_ARCH_CHIP_LM)") < checker.index("int run_all_mem_leak_checker(int checker_pid)")
assert "int TASK_PRCTL_ENTRY(int option, ...)" in prctl
assert "UP_MEM_LEAK_CAPTURE_SET_VARARG_BOUNDARY" not in arch
assert mm.count("int run_all_mem_leak_checker(int checker_pid);") == 1
assert "run_all_mem_leak_checker_with_capture" not in mm
assert "mlc_validate_current_capture" in roots
assert "mlc_validate_saved_task_roots" in roots
assert "mode == MLC_CONTEXT_IRQ" in roots
assert "mode != MLC_CONTEXT_REMOTE_PAUSED" in roots
assert "saved_sp = registers[REG_SP]" in roots
assert "registers[REG_CPSR]" in roots
assert "registers[REG_XPSR]" in roots
assert "processor_mode != 0x10u" in roots
assert "(cpsr & 0x00f00000u) != 0" in roots
assert "(xpsr & (1u << 24)) != 0" in roots
assert "(xpsr & 0x1ffu) == 0" in roots
assert "int mlc_test_run_saved_task_scan(" in test_seam
assert "mlc_validate_saved_task_roots(tcb, mode, expected_cpu, &roots)" in test_seam
assert "mlc_lifecycle_fail(&lifecycle, MLC_INCOMPLETE_TASK_CONTEXT)" in test_seam
assert "mlc_test_run_saved_task_scan_a" in production_fixture
assert "mlc_test_run_saved_task_scan_m" in production_fixture
assert "sched_foreach(scan_saved_task_roots, &scan)" in checker
assert "capture->caller_boundary" in checker
assert "MLC_INCOMPLETE_TASK_CONTEXT" in checker
assert "capture->stack_pointer < stack_low" in roots
assert "capture->tcb != (uint32_t)(uintptr_t)tcb" in roots
assert "capture->cpu != (uint32_t)cpu" in roots
assert roots.index("irq_try_enter_critical_fresh(&flags)") < roots.index("cpu = this_cpu();", roots.index("bool mlc_validate_current_capture"))
for path in (root / "os/arch/arm/src/armv7-m/arm_mem_leak_capture.S", root / "os/arch/arm/src/armv7-a/arm_mem_leak_capture.S"):
    text = path.read_text()
    assert text.count("{r0-r12, lr}") == 3
    for offset in range(0, 32, 4):
        assert f"UP_MEM_LEAK_CAPTURE_CALLEE_OFFSET + {offset}" in text
    assert "up_mem_leak_capture_identity" in text
    dispatch = text[text.index("prctl:"):]
    assert "cmp\tr0, #UP_MEM_LEAK_PRCTL_OPTION" in dispatch
    assert "task_prctl_impl" in dispatch
    direct = text[text.index("run_all_mem_leak_checker:"):text.index(".size run_all_mem_leak_checker")]
    assert direct.index("mov\tr1, r0") < direct.index("b\t.Lmem_leak_entry")
    assert "stmdb" not in direct and "sub\tsp" not in direct
    entry = text[text.index(".Lmem_leak_entry:"):]
    assert entry.index("stmdb\tsp!, {r0-r12, lr}") < entry.index("sub\tsp, sp, #UP_MEM_LEAK_CAPTURE_SIZE")
    assert entry.index("UP_MEM_LEAK_CAPTURE_SIZE + 56") < entry.index("run_all_mem_leak_checker_with_capture")
    assert dispatch.index("up_mem_leak_capture_identity") < dispatch.index("run_all_mem_leak_checker_with_capture")
    assert "UP_MEM_LEAK_CAPTURE_SIZE + 56" in dispatch
print("MLC_TASK8_WRAPPERS status=PASS prctl=assembly-entry direct=assembly-capture")
PY
	printf '%s\n' 'MLC_TASK8_ASSEMBLY status=PASS armv7_m=compiled armv7_a=compiled'
	printf '%s\n' 'MLC_TASK8_OBJECTS status=PASS armv7_m=3 armv7_a=3'
	printf '%s\n' 'MLC_TASK8_ENTRY_CONTRACT status=PASS direct_record=72 direct_entry_sp=true prctl_entry_sp=true clang_opts=O0,O1,O2,O3,Os gnu=unverified'
	printf '%s\n' 'MLC_TASK8_SAVED_STATUS status=PASS cpsr=usr,svc,sys xpsr=thumb'
	inject_transcript
	[ "${MLC_TASK8_TRANSCRIPT_INJECTION:-}" = missing ] || \
		printf '%s\n' 'MLC_TASK8_QEMU status=deferred_unexecuted_baseline_link_failure'
	;;
negative)
	negative_parent=$(mktemp -d /tmp/mlc-task8-negative.XXXXXX)
	dirty_root="$negative_parent/repository"
	$git_bin clone -q --no-hardlinks "$root" "$dirty_root"
	printf '\ndirty\n' >>"$dirty_root/tools/mem_leak_checker_scenarios/task-8.json"
	set +e
	"$dirty_root/tools/mem_leak_checker_task8_qa.sh" qemu --selector fixtures \
		--fixtures mlc_task_roots,mlc_direct_wrapper_roots --repeat 1 \
		--post-commit >"$negative_parent/dirty.out" 2>&1
	dirty_exit=$?
	set -e
	[ "$dirty_exit" -ne 0 ]
	! grep -q PASS "$negative_parent/dirty.out"
	set +e
	"$bash_bin" "$0" red >/dev/null 2>&1
	direct_red=$?
	set -e
	[ "$direct_red" -ne 0 ] && [ "$direct_red" -ne 86 ]
	set +e
	MLC_TASK8_INTERRUPT_AT_START=1 "$bash_bin" "$0" qemu --selector fixtures \
		--fixtures mlc_task_roots,mlc_direct_wrapper_roots --repeat 1 \
		--post-commit >/dev/null 2>&1
	interrupted=$?
	set -e
	[ "$interrupted" -ne 0 ]
	first=$("$bash_bin" "$0" qemu --selector fixtures --fixtures \
		mlc_task_roots,mlc_direct_wrapper_roots --repeat 1 --post-commit 2>"$model")
	[ ! -s "$model" ]
	second=$("$bash_bin" "$0" qemu --selector fixtures --fixtures \
		mlc_task_roots,mlc_direct_wrapper_roots --repeat 1 --post-commit 2>"$arm_m")
	[ ! -s "$arm_m" ]
	[ "$first" = "$second" ]
	$python_bin -I -B - "$scenario" <<'PY'
import copy, json, pathlib, sys
document = json.loads(pathlib.Path(sys.argv[1]).read_text())

def valid(value):
    return (value.get("schema") == 1 and value.get("task") == 8 and
            value.get("qemu") == "deferred_unexecuted_baseline_link_failure" and
            value.get("red_exempt") is False and
            value.get("red", [{}])[0].get("expected_exit") == 86 and
            all(len(value.get(bucket, [])) == 1 and
                value[bucket][0].get("expected_exit") == 0 and
                value[bucket][0].get("expected_records")
                for bucket in ("happy", "failure")))

assert valid(document)
for mutation in ("malformed", "stale", "misleading", "interrupted"):
    changed = copy.deepcopy(document)
    if mutation == "malformed":
        changed["task"] = "8"
    elif mutation == "stale":
        changed["red"][0]["expected_exit"] = 0
    elif mutation == "misleading":
        changed["qemu"] = "PASS"
    else:
        changed["failure"][0]["expected_records"] = []
    assert not valid(changed), mutation
PY
	printf '%s\n' 'MLC_TASK8_NEGATIVE status=PASS cases=malformed,stale,dirty,flaky,misleading,interruption,direct-red'
	;;
seal-authoritative)
	[ "$#" -eq 1 ] || exit 64
	receiving_sha=$(/usr/bin/git -C "$root" rev-parse "$1")
	receipt="$root/.omo/start-work/artifacts/task-8-executor/task-8-post-integration-$receiving_sha.json"
	case "${MLC_TASK8_CHILD_BASH_ENV:-}" in
		"") ;;
		/private/tmp/mlc-task5-seal-test.*/*|/tmp/mlc-task5-seal-test.*/*)
			export BASH_ENV=$MLC_TASK8_CHILD_BASH_ENV ;;
		*) exit 64 ;;
	esac
	case "${MLC_TASK8_CHILD_PATH:-}" in
		"") ;;
		/private/tmp/mlc-task5-seal-test.*/*|/tmp/mlc-task5-seal-test.*/*)
			PATH="$MLC_TASK8_CHILD_PATH:$trusted_path"; export PATH ;;
		*) exit 64 ;;
	esac
	receipt_sha=$(/usr/bin/env -u BASH_ENV -u ENV \
		$python_bin -I -B "$root/tools/mem_leak_checker_task8_authoritative.py" \
		"$root" "$receipt" "$receiving_sha")
	unset BASH_ENV
	PATH=$trusted_path
	export PATH
	$python_bin -I -B - "$receipt" "$receipt_sha" <<'PY'
import hashlib
import os
import stat
import sys

path, child_digest = sys.argv[1:]
descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0))
try:
    opened = os.fstat(descriptor)
    named = os.stat(path, follow_symlinks=False)
    if not stat.S_ISREG(opened.st_mode) or not stat.S_ISREG(named.st_mode):
        raise SystemExit("Task8 receipt is not a regular file")
    if (opened.st_dev, opened.st_ino) != (named.st_dev, named.st_ino):
        raise SystemExit("Task8 receipt named inode changed")
    digest = hashlib.sha256()
    while chunk := os.read(descriptor, 65536):
        digest.update(chunk)
    if digest.hexdigest() != child_digest:
        raise SystemExit("Task8 receipt descriptor digest mismatch")
finally:
    os.close(descriptor)
PY
	printf 'MLC_QA_SEAL task=8 status=host_scenarios_sealed_qemu_explicitly_deferred receiving_sha=%s receipt=%s receipt_sha256=%s publication=exclusive_final_inode_weaker_exfat\n' \
		"$receiving_sha" "$receipt" "$receipt_sha"
	;;
*) exit 64 ;;
esac
