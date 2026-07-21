#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
source_root=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/tizenrt-mlc-report-test.XXXXXX")
binary=$build_dir/test_mem_leak_checker_report

trap 'rm -f "$binary"; rmdir "$build_dir" 2>/dev/null || true' EXIT HUP INT TERM

[ "$#" -ge 2 ] || exit 64
option=$1
selection=$2
shift 2
case "$option" in
	--fixture)
		case "$selection" in *,*) exit 64 ;; esac
		;;
	--fixtures) ;;
	*) exit 64 ;;
esac
case "$selection" in
	""|,*|*,|*,,*) exit 64 ;;
esac
old_ifs=$IFS
IFS=,
set -- $selection
IFS=$old_ifs
seen=,
for fixture do
	case "$fixture" in
		mlc_report_contract|mlc_reason_precedence|mlc_legacy_row_bounds) ;;
		*) exit 64 ;;
	esac
	case "$seen" in *,"$fixture",*) exit 64 ;; esac
	seen=$seen$fixture,
done

${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic \
	-I"$source_root/os/kernel/debug" \
	"$source_root/os/kernel/debug/tests/test_mem_leak_checker_report.c" \
	"$source_root/os/kernel/debug/mem_leak_checker_report.c" \
	-o "$binary"

records=$($binary)
printf '%s\n' "$records"
printf '%s\n' "$records" | while IFS= read -r record; do
	case "$record" in
		MLC_HOST\ fixture=mlc_report_contract\ status=PASS|\
		MLC_HOST\ fixture=mlc_reason_precedence\ status=PASS|\
		MLC_HOST\ fixture=mlc_legacy_row_bounds\ status=PASS) ;;
		*) exit 1 ;;
	esac
done
