#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
source_root=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/tizenrt-mlc-candidates.XXXXXX")
binary=$build_dir/test_mem_leak_checker_candidates
compile_log=$build_dir/compile.log

trap 'unlink "$binary" "$compile_log" 2>/dev/null || true; rmdir "$build_dir" 2>/dev/null || true' EXIT HUP INT TERM

[ "$#" -eq 2 ] || exit 64
selector=$1
selection=$2
case "$selector" in
	--fixture) case "$selection" in *,*) exit 64 ;; esac ;;
	--fixtures) ;;
	*) exit 64 ;;
esac
case "$selection" in ""|,*|*,|*,,*) exit 64 ;; esac

if ${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic \
	-I"$source_root/tools/mem_leak_checker_task7_stubs" \
	-I"$source_root/os/kernel/debug" \
	"$source_root/tools/tests/test_mem_leak_checker_candidates.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_candidates.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_candidate_roots.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_candidate_validate.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_candidate_preflight.c" \
	"$source_root/tools/tests/mem_leak_checker_clock_stub.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_budget.c" \
	-o "$binary" >"$compile_log" 2>&1; then
	if [ "${MLC_TASK7_EXPECT_RED:-0}" = 1 ]; then
		printf '%s\n' 'MLC_TASK7_RED unexpected_green=true' >&2
		exit 1
	fi
else
	if [ "${MLC_TASK7_EXPECT_RED:-0}" = 1 ] && \
		rg -q 'mem_leak_checker_candidates\.(h|c)' "$compile_log"; then
		printf '%s\n' 'MLC_TASK7_RED status=expected_failure reason=collector_missing exit=86'
		exit 86
	fi
	cat "$compile_log" >&2
	exit 1
fi

old_ifs=$IFS
IFS=,
set -- $selection
IFS=$old_ifs
seen=,
for fixture do
	case "$fixture" in
		mlc_candidate_manifest|mlc_control_source_target_exclusions|\
		mlc_heap_backed_loadable_root|mlc_candidate_corruption_and_lock_contention|\
		mlc_tcb_stack_root_policy|mlc_invalid_loadable_root_container) ;;
		*) exit 64 ;;
	esac
	case "$seen" in *,"$fixture",*) exit 64 ;; esac
	seen=$seen$fixture,
	"$binary" "$fixture"
done
