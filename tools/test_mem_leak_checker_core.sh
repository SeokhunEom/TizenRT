#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
source_root=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/tizenrt-mlc-core-test.XXXXXX")
binary=$build_dir/test_mem_leak_checker_core

trap 'unlink "$binary" 2>/dev/null || true; rmdir "$build_dir" 2>/dev/null || true' EXIT HUP INT TERM

[ "$#" -eq 2 ] || exit 64
option=$1
selection=$2
case "$option" in
	--fixture)
		case "$selection" in
			*,*) exit 64 ;;
		esac
		;;
	--fixtures) ;;
	*) exit 64 ;;
esac
case "$selection" in
	""|,*|*,|*,,*) exit 64 ;;
esac

${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic \
	-I"$source_root/os/kernel/debug" \
	"$source_root/os/kernel/debug/tests/test_mem_leak_checker_core.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_core.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_index.c" \
	"$source_root/tools/tests/mem_leak_checker_clock_stub.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_budget.c" \
	-o "$binary"

old_ifs=$IFS
IFS=,
set -- $selection
IFS=$old_ifs
seen=,
for fixture do
	case "$fixture" in
		mlc_scanner_index|mlc_zero_exact_precedence|mlc_scanner_invalid_ranges) ;;
		*) exit 64 ;;
	esac
	case "$seen" in
		*,"$fixture",*) exit 64 ;;
	esac
	seen=$seen$fixture,
	"$binary" "$fixture"
done
