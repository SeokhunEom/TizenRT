#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
source_root=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/tizenrt-mlc-graph-test.XXXXXX")
binary=$build_dir/test_mem_leak_checker_graph

trap 'unlink "$binary" 2>/dev/null || true; rmdir "$build_dir" 2>/dev/null || true' EXIT HUP INT TERM

[ "$#" -ge 2 ] || exit 64
option=$1
selection=$2
shift 2
repeat=1
if [ "$#" -gt 0 ]; then
	[ "$#" -eq 2 ] && [ "$1" = --repeat ] || exit 64
	repeat=$2
	case "$repeat" in *[!0-9]*|'') exit 64 ;; esac
	[ "$repeat" -gt 0 ] || exit 64
fi
case "$option" in
	--fixture) case "$selection" in *,*) exit 64 ;; esac ;;
	--fixtures) ;;
	*) exit 64 ;;
esac
case "$selection" in ""|,*|*,|*,,*) exit 64 ;; esac

old_ifs=$IFS
IFS=,
set -- $selection
IFS=$old_ifs
seen=,
for fixture do
	case "$fixture" in
		mlc_graph_core|mlc_zero_graph|mlc_cross_heap_root_chain|\
		mlc_frontier_tarjan_exhaustion|mlc_tarjan_max_depth) ;;
		*) exit 64 ;;
	esac
	case "$seen" in *,"$fixture",*) exit 64 ;; esac
	seen=$seen$fixture,
done

${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic \
	-I"$source_root/os/kernel/debug" \
	"$source_root/os/kernel/debug/tests/test_mem_leak_checker_graph.c" \
	"$source_root/os/kernel/debug/tests/test_mem_leak_checker_graph_depth.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_core.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_index.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_graph.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_graph_validate.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_tarjan.c" \
	"$source_root/tools/tests/mem_leak_checker_clock_stub.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_budget.c" \
	-o "$binary"

iteration=1
while [ "$iteration" -le "$repeat" ]; do
	for fixture do
		"$binary" "$fixture"
	done
	iteration=$((iteration + 1))
done
