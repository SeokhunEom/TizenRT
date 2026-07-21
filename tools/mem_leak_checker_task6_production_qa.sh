#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/mlc-task6-production.XXXXXX")
trap 'rm -rf -- "$temp_dir"' EXIT HUP INT TERM
stub="$repo_root/tools/mem_leak_checker_task6_native_stubs"

common_flags=(
	-std=c11 -Wall -Wextra -Werror -Wno-unused-function
	-Wno-unused-parameter -Wno-implicit-function-declaration
	-Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
	-Wno-incompatible-pointer-types -Wno-int-conversion -Wno-sign-compare
	-ffunction-sections -fdata-sections -Dnoreturn_function=__attribute__\(\(noreturn\)\)
	-DCONFIG_BINFMT_ENABLE -DCONFIG_BINFMT_LOADABLE
	-DCONFIG_APP_BINARY_SEPARATION -D__KERNEL__ -DCONFIG_BINARY_MANAGER
	-DCONFIG_NUM_APPS=3 -DCONFIG_KMM_NHEAPS=2
	-DCONFIG_HEAP_INDEX_LOADED_APP=1 -DCONFIG_MEM_LEAK_CHECKER -DCODE=
	-I"$stub" -I"$repo_root/os/kernel" -I"$repo_root/os/binfmt"
	-I"$repo_root/os/kernel/debug" -I"$repo_root/os/mm/mm_heap"
	-I"$repo_root/tools" -idirafter "$repo_root/os/include"
)
production_sources=(
	os/binfmt/binfmt_execmodule.c
	os/binfmt/binfmt_loadbinary.c
	os/binfmt/binfmt_exit.c
	os/kernel/binary_manager/binary_manager_load.c
	os/mm/mm_heap/mm_loadable_domain.c
	os/mm/mm_heap/mm_loadable_domain_pin.c
	os/kernel/debug/mem_leak_checker.c
	os/kernel/debug/mem_leak_checker_domain.c
	os/kernel/debug/mem_leak_checker_lifecycle.c
	os/kernel/debug/mem_leak_checker_budget.c
	os/kernel/debug/mem_leak_checker_pause_owner.c
	os/arch/arm/src/common/up_restoretask.c
)

require_symbol() {
	local object=$1
	local wanted=$2
	nm "$object" | awk -v symbol="$wanted" \
		'$NF == symbol || $NF == "_" symbol { count++ } \
		 END { exit count == 1 ? 0 : 1 }'
}

build_variant() {
	local variant=$1
	shift
	local directory="$temp_dir/$variant"
	local source object
	local objects=()
	local source_flags=()
	mkdir -p "$directory"
	for source in "${production_sources[@]}"; do
		source_flags=()
		if [[ "$source" == os/kernel/debug/mem_leak_checker.c ]]; then
			source_flags=(-Wno-format)
		fi
		object="$directory/$(basename "${source%.c}").o"
		cc "${common_flags[@]}" "${source_flags[@]}" "$@" \
			-c "$repo_root/$source" -o "$object"
		objects+=("$object")
	done
	cc -r "${objects[@]}" -o "$directory/production-linked.o"
	for source in exec_module binfmt_register_app_domain load_binary binfmt_exit \
		binary_manager_execute_loader mm_loadable_domain_register \
		mm_loadable_domain_try_pin_all mlc_domain_guard_acquire \
		mlc_lifecycle_begin run_mem_leak_checker; do
		require_symbol "$directory/production-linked.o" "$source"
	done
	if [[ "$variant" == common || "$variant" == optimized_unified ]]; then
		for source in binfmt_exchange_umm_app_id binfmt_update_umm_app_id \
			up_restoretask; do
			require_symbol "$directory/production-linked.o" "$source"
		done
	fi
}

build_variant xip_loadable -DCONFIG_XIP_ELF
build_variant non_xip_loadable
build_variant common -DCONFIG_SUPPORT_COMMON_BINARY
build_variant optimized_unified -DCONFIG_SUPPORT_COMMON_BINARY \
	-DCONFIG_OPTIMIZE_APP_RELOAD_TIME \
	-DCONFIG_BINFMT_SECTION_UNIFIED_MEMORY

runtime="$temp_dir/runtime"
mkdir -p "$runtime"
runtime_flags=(-DCONFIG_XIP_ELF -DCONFIG_SUPPORT_COMMON_BINARY)
for source in os/binfmt/binfmt_execmodule.c os/binfmt/binfmt_loadbinary.c \
	os/mm/mm_heap/mm_loadable_domain.c \
	os/mm/mm_heap/mm_loadable_domain_pin.c \
	tools/mem_leak_checker_task6_native_support.c \
	tools/mem_leak_checker_task6_production_test.c; do
	extra_flags=()
	if [[ "$source" == tools/mem_leak_checker_task6_native_support.c ]]; then
		extra_flags=(-DMLC_TASK6_PRODUCTION_SUPPORT)
	fi
	cc "${common_flags[@]}" "${runtime_flags[@]}" "${extra_flags[@]}" -pthread \
		-c "$repo_root/$source" -o "$runtime/$(basename "${source%.c}").o"
done
case $(uname -s) in
	Darwin) linker_flags=(-Wl,-undefined,dynamic_lookup -Wl,-dead_strip) ;;
	*) linker_flags=(-Wl,--gc-sections) ;;
esac
cc -pthread "${linker_flags[@]}" "$runtime"/*.o -o "$runtime/production-test"
"$runtime/production-test"
printf '%s\n' \
	'MLC_TASK6_PRODUCTION_MATRIX status=PASS variants=xip_loadable,non_xip_loadable,common,optimized_unified xip_ranges=within,partial,outside,invalid identity_quiescence=true'
