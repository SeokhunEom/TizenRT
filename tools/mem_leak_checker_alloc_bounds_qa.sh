#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
source_root=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
compiler=${CC:-clang}
temp_root=${TMPDIR:-/tmp}
temp_dir=$(mktemp -d "$temp_root/mlc-alloc-bounds.XXXXXX")

cleanup() {
	case "$temp_dir" in
		"$temp_root"/mlc-alloc-bounds.*) rm -rf -- "$temp_dir" ;;
		*) return 1 ;;
	esac
}
trap cleanup EXIT HUP INT TERM

common_flags=(-std=c11 -Wall -Wextra -Werror)
sanitizer_flags=(-fsanitize=address,undefined -fno-omit-frame-pointer)
include_flags=(-I"$source_root/os/mm/mm_heap" -idirafter "$source_root/os/include")
if [ "$#" -eq 0 ]; then
	requested_fixtures=mlc_alloc_bounds,mlc_alloc_zero,mlc_alloc_padding_invalid,mlc_alloc_padding_unrepresentable
elif [ "$#" -eq 1 ] && [ "$1" = --fixtures ]; then
	printf '%s\n' "missing value for --fixtures" >&2
	exit 64
elif [ "$#" -eq 2 ] && [ "$1" = --fixtures ] && [ -n "$2" ]; then
	requested_fixtures=$2
else
	printf '%s\n' "usage: $0 [--fixtures comma-separated-list]" >&2
	exit 64
fi
case "$requested_fixtures" in
	,*|*,|*,,*) printf '%s\n' "malformed fixture list: $requested_fixtures" >&2; exit 64 ;;
esac

fixture_selected() {
	case ",$requested_fixtures," in
		*,"$1",*) return 0 ;;
		*) return 1 ;;
	esac
}

old_ifs=$IFS
IFS=,
for fixture in $requested_fixtures; do
	case "$fixture" in
		mlc_alloc_bounds|mlc_alloc_zero|mlc_alloc_padding_invalid|mlc_alloc_padding_unrepresentable) ;;
		*) printf '%s\n' "unknown fixture: $fixture" >&2; exit 1 ;;
	esac
done
IFS=$old_ifs

if fixture_selected mlc_alloc_bounds; then
	"$compiler" "${common_flags[@]}" "${sanitizer_flags[@]}" \
		"$source_root/tools/tests/mem_leak_checker_alloc_layout_characterization.c" \
		-o "$temp_dir/layout"
	"$temp_dir/layout"
	"$compiler" "${common_flags[@]}" "${sanitizer_flags[@]}" "${include_flags[@]}" \
		"$source_root/tools/tests/mem_leak_checker_alloc_bounds_model.c" \
		-o "$temp_dir/bounds"
	"$temp_dir/bounds" mlc_alloc_bounds
	"$compiler" "${common_flags[@]}" "${sanitizer_flags[@]}" "${include_flags[@]}" \
		"$source_root/tools/tests/mem_leak_checker_realloc_mutation_harness.c" \
		-o "$temp_dir/fake_heap"
	"$temp_dir/fake_heap"

	for mutation in boundary preference; do
		mkdir "$temp_dir/$mutation"
		cp "$source_root/os/mm/mm_heap/mm_realloc_logic.h" "$temp_dir/$mutation/mm_realloc_logic.h"
		if [ "$mutation" = boundary ]; then
			perl -pi -e 's/< free_node_size/<= free_node_size/g' "$temp_dir/$mutation/mm_realloc_logic.h"
		else
			perl -pi -e 's/next_size >= previous_size/next_size <= previous_size/' "$temp_dir/$mutation/mm_realloc_logic.h"
		fi
		"$compiler" "${common_flags[@]}" "${sanitizer_flags[@]}" \
			-I"$temp_dir/$mutation" -idirafter "$source_root/os/include" \
			"$source_root/tools/tests/mem_leak_checker_alloc_bounds_model.c" \
			-o "$temp_dir/mutant-$mutation"
		if bash -c '"$1" mlc_alloc_bounds' _ "$temp_dir/mutant-$mutation" \
				>/dev/null 2>&1; then
			printf 'mutation unexpectedly survived: %s\n' "$mutation" >&2
			exit 1
		fi
	done

	test "$(rg -n "mm_allocnode_set_padding" "$source_root/os/mm/mm_heap/mm_malloc.c" | wc -l | tr -d ' ')" = 1
	test "$(rg -n "mm_allocnode_set_padding" "$source_root/os/mm/mm_heap/mm_memalign.c" | wc -l | tr -d ' ')" = 1
	test "$(rg -n "mm_realloc_publish_padding" "$source_root/os/mm/mm_heap/mm_realloc.c" | wc -l | tr -d ' ')" = 2
	rg -q "MM_MALLOC_PADDING_MAX" "$source_root/os/mm/mm_heap/mm_malloc.c"
	rg -q "MM_MEMALIGN_PADDING_MAX" "$source_root/os/mm/mm_heap/mm_memalign.c"
	rg -q "MM_REALLOC_PADDING_MAX" "$source_root/os/mm/mm_heap/mm_realloc.c"
	rg -q "mm_realloc_plan" "$source_root/os/mm/mm_heap/mm_realloc.c"
	rg -q "mm_realloc_copy" "$source_root/os/mm/mm_heap/mm_realloc.c" "$source_root/os/mm/umm_heap/umm_realloc.c" "$source_root/os/mm/kmm_heap/kmm_realloc.c"
	if rg -q "MM_MEMORY_STATE_|->memory_state" "$source_root/os" "$source_root/apps/examples/testcase/le_tc/kernel/tc_mem_leak_checker.c"; then
		exit 1
	fi
fi

if fixture_selected mlc_alloc_zero; then
	"$compiler" "${common_flags[@]}" "${sanitizer_flags[@]}" "${include_flags[@]}" \
		"$source_root/tools/tests/mem_leak_checker_alloc_bounds_model.c" \
		-o "$temp_dir/zero"
	"$temp_dir/zero" mlc_alloc_zero
	rg -q "return umm_malloc_with_caller\(size, caller_retaddr\)" \
		"$source_root/os/mm/umm_heap/umm_realloc.c"
	rg -q "do \{" "$source_root/os/mm/umm_heap/umm_malloc.c"
	rg -q "brkaddr = sbrk\(size\)" "$source_root/os/mm/umm_heap/umm_malloc.c"
	if rg -n "mm_manage_alloc_fail.*n \* elem_size" \
			"$source_root/os/mm/umm_heap/umm_calloc.c" \
			"$source_root/os/mm/kmm_heap/kmm_calloc.c"; then
		exit 1
	fi
fi

if fixture_selected mlc_alloc_padding_invalid; then
	"$compiler" "${common_flags[@]}" "${sanitizer_flags[@]}" "${include_flags[@]}" \
		"$source_root/tools/tests/mem_leak_checker_alloc_bounds_model.c" \
		-o "$temp_dir/invalid"
	"$temp_dir/invalid" mlc_alloc_padding_invalid
fi

if fixture_selected mlc_alloc_padding_unrepresentable; then
	if "$compiler" "${common_flags[@]}" \
			"$source_root/tools/tests/mem_leak_checker_alloc_padding_unrepresentable.c" \
			-o "$temp_dir/unrepresentable" 2>"$temp_dir/unrepresentable.log"; then
		printf '%s\n' "compile-negative unexpectedly succeeded" >&2
		exit 1
	fi
	rg -q "allocation padding bound exceeds uint16_t" "$temp_dir/unrepresentable.log"
fi

for fixture in mlc_alloc_bounds mlc_alloc_zero mlc_alloc_padding_invalid mlc_alloc_padding_unrepresentable; do
	if fixture_selected "$fixture"; then
		printf 'MLC_QA_HOST fixture=%s status=PASS\n' "$fixture"
	fi
done
