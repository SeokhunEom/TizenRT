#!/bin/bash -p
set -euo pipefail

mode=${1:?usage: mem_leak_checker_task6_host_qa.sh host|fatal ROOT TEMP [REPEAT]}
repo_root=${2:?repository root required}
temp_dir=${3:?temporary directory required}
python_bin=/opt/homebrew/bin/python3

run_host_contracts() {
	local repeat=$1
	local stub="$repo_root/tools/mem_leak_checker_task6_stubs"
	local production_stderr warning line warning_count
	"$repo_root/tools/mem_leak_checker_task6_native_qa.sh"
	production_stderr="$temp_dir/production.stderr"
	"$repo_root/tools/mem_leak_checker_task6_production_qa.sh" 2>"$production_stderr"
	if [ -s "$production_stderr" ]; then
		warning='ld: warning: /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib/libSystem.tbd, ignoring unexpected dylib text stub file'
		warning_count=0
		while IFS= read -r line; do
			[ "$line" = "$warning" ] || exit 1
			warning_count=$((warning_count + 1))
		done <"$production_stderr"
		[ "$warning_count" -eq 4 ] || exit 1
	fi

	cc -std=c11 -Wall -Wextra -Werror -pthread -I"$stub" \
		-Dmain=sem_fixture_main -c \
		"$repo_root/tools/mem_leak_checker_task6_sem_test.c" \
		-o "$temp_dir/sem-support.o"
	for source in mm_sem mm_loadable_domain mm_loadable_domain_pin; do
		cc -std=c11 -Wall -Wextra -Werror -pthread -I"$stub" -c \
			"$repo_root/os/mm/mm_heap/$source.c" -o "$temp_dir/$source.o"
	done
	cc -std=c11 -Wall -Wextra -Werror -pthread -I"$stub" -c \
		-I"$repo_root/os/mm/mm_heap" \
		"$repo_root/tools/mem_leak_checker_task6_domain_test.c" \
		-o "$temp_dir/domain-test.o"
	cc -pthread "$temp_dir/sem-support.o" "$temp_dir/mm_sem.o" \
		"$temp_dir/mm_loadable_domain.o" "$temp_dir/mm_loadable_domain_pin.o" \
		"$temp_dir/domain-test.o" -o "$temp_dir/domain-test"
	"$temp_dir/domain-test" "$repeat"

	cc -std=c11 -Wall -Wextra -Werror -pthread -I"$stub" \
		-I"$repo_root/os/mm/mm_heap" \
		"$repo_root/os/mm/mm_heap/mm_loadable_domain.c" \
		"$repo_root/os/mm/mm_heap/mm_loadable_domain_pin.c" \
		"$repo_root/tools/mem_leak_checker_task6_publish_test.c" \
		-o "$temp_dir/publish-test"
	"$temp_dir/publish-test"

	cc -std=c11 -Wall -Wextra -Werror -I"$stub" \
		-I"$repo_root/os/kernel/debug" -c \
		"$repo_root/os/kernel/debug/mem_leak_checker_domain.c" \
		-o "$temp_dir/domain-guard.o"
	cc -std=c11 -Wall -Wextra -Werror -I"$stub" \
		-I"$repo_root/os/kernel/debug" -c \
		"$repo_root/os/kernel/debug/mem_leak_checker_lifecycle.c" \
		-o "$temp_dir/lifecycle.o"
	cc -std=c11 -Wall -Wextra -Werror -I"$stub" \
		-I"$repo_root/os/kernel/debug" -c \
		"$repo_root/os/kernel/debug/mem_leak_checker_budget.c" \
		-o "$temp_dir/budget.o"
	cc -std=c11 -Wall -Wextra -Werror -I"$stub" \
		-I"$repo_root/os/kernel/debug" -c \
		"$repo_root/os/kernel/debug/mem_leak_checker_pause_owner.c" \
		-o "$temp_dir/pause-owner.o"
	cc -std=c11 -Wall -Wextra -Werror -pthread -I"$stub" \
		-I"$repo_root/os/kernel/debug" -I"$repo_root/os/mm/mm_heap" -c \
		"$repo_root/tools/mem_leak_checker_task6_guard_test.c" \
		-o "$temp_dir/domain-guard-test.o"
	cc -pthread "$temp_dir/sem-support.o" "$temp_dir/mm_sem.o" \
		"$temp_dir/mm_loadable_domain.o" "$temp_dir/mm_loadable_domain_pin.o" \
		"$temp_dir/domain-guard.o" "$temp_dir/lifecycle.o" \
		"$temp_dir/budget.o" "$temp_dir/pause-owner.o" "$temp_dir/domain-guard-test.o" \
		-o "$temp_dir/domain-guard-test"
	"$temp_dir/domain-guard-test" "$repeat"

	for mutation in attempts7 attempts9 usec9999 usec10001; do
		case "$mutation" in
			attempts7) expression='s/MLC_DOMAIN_ACQUIRE_ATTEMPTS 8/MLC_DOMAIN_ACQUIRE_ATTEMPTS 7/' ;;
			attempts9) expression='s/MLC_DOMAIN_ACQUIRE_ATTEMPTS 8/MLC_DOMAIN_ACQUIRE_ATTEMPTS 9/' ;;
			usec9999) expression='s/MLC_DOMAIN_ACQUIRE_MAX_USEC 10000/MLC_DOMAIN_ACQUIRE_MAX_USEC 9999/' ;;
			usec10001) expression='s/MLC_DOMAIN_ACQUIRE_MAX_USEC 10000/MLC_DOMAIN_ACQUIRE_MAX_USEC 10001/' ;;
		esac
		sed "$expression" "$repo_root/os/kernel/debug/mem_leak_checker_domain.c" \
			>"$temp_dir/domain-$mutation.c"
		cc -std=c11 -Wall -Wextra -Werror -I"$stub" \
			-I"$repo_root/os/kernel/debug" -c "$temp_dir/domain-$mutation.c" \
			-o "$temp_dir/domain-$mutation.o"
		cc -pthread "$temp_dir/sem-support.o" "$temp_dir/mm_sem.o" \
			"$temp_dir/mm_loadable_domain.o" "$temp_dir/mm_loadable_domain_pin.o" \
			"$temp_dir/domain-$mutation.o" "$temp_dir/lifecycle.o" \
			"$temp_dir/budget.o" "$temp_dir/pause-owner.o" "$temp_dir/domain-guard-test.o" \
			-o "$temp_dir/test-$mutation"
		if sh -c '"$1" 1 >/dev/null 2>&1' _ \
			"$temp_dir/test-$mutation" >/dev/null 2>&1; then
			exit 1
		fi
	done
	printf '%s\n' 'MLC_TASK6_BOUNDARY_MUTANTS status=PASS rejected=attempts7,attempts9,usec9999,usec10001'

	if cc -std=c11 -DCONFIG_SMP \
		-I"$repo_root/tools/mem_leak_checker_task5_stubs" -c \
		"$repo_root/os/kernel/irq/irq_trycritical.c" \
		-o "$temp_dir/unsupported.o" >"$temp_dir/unsupported.log" 2>&1; then
		exit 1
	fi
	grep -q 'CONFIG_SMP requires CONFIG_IRQCOUNT' "$temp_dir/unsupported.log"
	"$repo_root/tools/mem_leak_checker_alloc_bounds_qa.sh" --fixtures \
		mlc_alloc_bounds,mlc_alloc_zero,mlc_alloc_padding_invalid,mlc_alloc_padding_unrepresentable \
		>/dev/null
	"$repo_root/tools/test_mem_leak_checker_core.sh" --fixtures \
		mlc_scanner_index,mlc_zero_exact_precedence,mlc_scanner_invalid_ranges \
		>/dev/null
	"$repo_root/tools/test_mem_leak_checker_graph.sh" --fixtures \
		mlc_graph_core,mlc_zero_graph --repeat 50 >/dev/null
	"$repo_root/tools/test_mem_leak_checker_graph.sh" --fixtures \
		mlc_frontier_tarjan_exhaustion,mlc_tarjan_max_depth >/dev/null
	"$repo_root/tools/mem_leak_checker_task5_qa.sh" "$temp_dir/task5-actual" \
		>/dev/null
	printf '%s\n' 'MLC_TASK6_REGRESSIONS status=PASS tasks=2,3,4,5'

	"$python_bin" -I -B - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
mm_sem = (root / "os/mm/mm_heap/mm_sem.c").read_text()
domain = (root / "os/kernel/debug/mem_leak_checker_domain.c").read_text()
domain_pin = (root / "os/mm/mm_heap/mm_loadable_domain_pin.c").read_text()
graph_header = (root / "os/kernel/debug/mem_leak_checker_graph.h").read_text()
candidate_header = (root / "os/kernel/debug/mem_leak_checker_candidates.h").read_text()
unified_source = (root / "os/kernel/debug/mem_leak_checker_unified.c").read_text()
kconfig = (root / "apps/system/mem_leak_checker/Kconfig").read_text()
hash_config = kconfig[kconfig.index("config MEM_LEAK_CHECKER_HASH_TABLE_SIZE"):kconfig.index("config MEM_LEAK_CHECKER_MAX_ALLOC_COUNT")]
assert "range 1 65536" in hash_config
max_alloc_config = kconfig[kconfig.index("config MEM_LEAK_CHECKER_MAX_ALLOC_COUNT"):kconfig.index("config MEM_LEAK_REMOTE_PAUSED_MAX_POLLS")]
assert "range 1 65536" in max_alloc_config
assert "struct mlc_graph_root_range_s" in graph_header
assert "struct mlc_root_range_s" not in graph_header
assert "struct mlc_root_range_s" in candidate_header
assert "struct mlc_graph_root_range_s g_unified_roots" in unified_source
assert "struct mlc_root_range_s" not in unified_source
checker = (root / "os/kernel/debug/mem_leak_checker.c").read_text()
assert "#define MLC_SNAPSHOT_FIXED_EXCLUSION_CAPACITY 32u" in checker
assert "mlc_exclude_report_buffers" in checker
assert "key = (uintptr_t)g_node_info[index].node % HASH_SIZE;" in checker
assert "key = (uintptr_t)node % HASH_SIZE;" in checker
assert "long key" not in checker
execute = (root / "os/binfmt/binfmt_execmodule.c").read_text()
exit_path = (root / "os/binfmt/binfmt_exit.c").read_text()
common_load = (root / "os/binfmt/binfmt_loadbinary.c").read_text()
common_unload = (root / "os/kernel/binary_manager/binary_manager_load.c").read_text()
restore_task = (root / "os/arch/arm/src/common/up_restoretask.c").read_text()
sem_trywait = (root / "os/kernel/semaphore/sem_trywait.c").read_text()
sem_post = (root / "os/kernel/semaphore/sem_post.c").read_text()
sem_holder = (root / "os/kernel/semaphore/sem_holder.c").read_text()
fresh = mm_sem[mm_sem.index("int mm_trysemaphore_fresh"):mm_sem.index("bool mm_takesemaphore")]
assert fresh.count("sem_trywait(&heap->mm_semaphore)") == 1
assert "sem_wait(" not in fresh and "mm_takesemaphore(" not in fresh
assert "sem_addholder(sem)" in sem_trywait and "save_semaphore_history" in sem_trywait
assert "sem_releaseholder" in sem_post and "save_semaphore_history" in sem_post
assert "sem_findorallocateholder" in sem_holder and "sem_freeholder" in sem_holder
assert domain.index("MLC_RESOURCE_DOMAIN") < domain.index("MLC_RESOURCE_CRITICAL")
assert domain.index("MLC_RESOURCE_CRITICAL") < domain.index("MLC_RESOURCE_HEAP")
assert "printf(" not in domain and "malloc(" not in domain and "sem_wait(" not in domain
assert "mm_loadable_domain_lock(" not in domain_pin[domain_pin.index("int mm_loadable_domain_unpin_all"):]
assert "sem_wait(" not in domain_pin
assert domain_pin.count("__ATOMIC_SEQ_CST") >= 7
fill = checker[checker.index("static void fill_hash_table"):checker.index("static void search_addr")]
capture = checker[checker.index("static int capture_info"):checker.index("static void print_heap_report")]
owned = checker[checker.index("static int run_mem_leak_checker_owned"):checker.index("int run_mem_leak_checker")]
assert "mm_takesemaphore" not in fill + capture and "mm_givesemaphore" not in fill + capture
assert "g_test_observer(" not in owned
assert "get_bin_addr_list" not in checker and "mm_get_app_heap_with_name" not in checker
assert execute.index("mm_loadable_domain_register") < execute.index("mm_loadable_domain_activate")
activate_domain = execute.index("mm_loadable_domain_activate")
activate_task = execute.index("task_activate", activate_domain)
assert activate_domain < activate_task
rollback = execute[execute.index("errout_with_appheap:"):execute.index("return ret;", execute.index("errout_with_appheap:"))]
assert rollback.index("mm_loadable_domain_disable_and_wait") < rollback.index("mm_remove_app_heap_list")
assert "PANIC();" in rollback and "(void)mm_loadable_domain_" not in rollback
assert exit_path.index("mm_loadable_domain_disable_and_wait") < exit_path.index("unload_module(bin)")
assert exit_path.index("mm_loadable_domain_finish_unload") < exit_path.index("kmm_free(bin)")
exit_unload_failure = exit_path[exit_path.index("if (ret < 0) {"):exit_path.index("elf_delete_bin_section_addr")]
assert "mm_loadable_domain_reactivate" in exit_unload_failure and "PANIC();" in exit_unload_failure
exit_finalize = exit_path[exit_path.index("mm_loadable_domain_finish_unload"):exit_path.index("/* Free the load structure */")]
assert "PANIC();" in exit_finalize
assert common_load.index("mm_loadable_domain_register") < common_load.index("mm_loadable_domain_activate")
common_activate = common_load.index("mm_loadable_domain_activate")
for publication in ("binfmt_exchange_umm_app_id(", "BIN_STATE(binary_idx) = BINARY_RUNNING",
                    "BIN_LOADINFO(binary_idx) = bin", "BIN_LOAD_ATTR(binary_idx) = *load_attr",
                    "strncpy(BIN_NAME(binary_idx)"):
    assert common_load.index(publication) < common_activate
assert "domain.ready = binfmt_common_domain_ready" in common_load
assert "writable_container = bin->ramstart" in common_load
assert common_unload.index("mm_loadable_domain_disable_and_wait") < common_unload.index("unload_module(g_lib_binp)")
assert common_unload.index("binfmt_exchange_umm_app_id(NULL)") < common_unload.index("unload_module(g_lib_binp)")
unload_failure = common_unload[common_unload.index("ret = unload_module(g_lib_binp)"):common_unload.index("BIN_STATE(bin_idx) = BINARY_INACTIVE")]
assert unload_failure.index("binfmt_exchange_umm_app_id(umm_app_id)") < unload_failure.index("mm_loadable_domain_reactivate")
assert unload_failure.count("PANIC();") >= 2
exchange = common_load[common_load.index("binfmt_exchange_umm_app_id"):common_load.index("binfmt_umm_app_id_is")]
update = common_load[common_load.index("binfmt_update_umm_app_id"):common_load.index("#endif", common_load.index("binfmt_update_umm_app_id"))]
assert "spin_lock_irqsave" in exchange and "spin_unlock_irqrestore" in exchange
assert "spin_lock_irqsave" in update and "spin_unlock_irqrestore" in update
assert "binfmt_update_umm_app_id(tcb->app_id)" in restore_task
assert "writable_container_size = binp->sizes[BIN_TEXT] +" in execute
assert "binp->sizes[BIN_RO] + binp->ramsize" in execute
print("MLC_TASK6_STATIC status=PASS production_hooks=true production_loader_gate=true native_holder_pi_history=true protected_wait_alloc_print=false attempts=8 max_usec=10000")
PY
}

run_fatal_contracts() {
	local stub="$repo_root/tools/mem_leak_checker_task6_stubs"
	local fatal_stub="$repo_root/tools/mem_leak_checker_task6_fatal_stubs"
	local source

	cc -std=c11 -Wall -Wextra -Werror -pthread -I"$stub" \
		-Dmain=sem_fixture_main -c \
		"$repo_root/tools/mem_leak_checker_task6_sem_test.c" \
		-o "$temp_dir/fatal-sem-support.o"
	for source in mm_sem mm_loadable_domain mm_loadable_domain_pin; do
		cc -std=c11 -Wall -Wextra -Werror -pthread -I"$stub" -c \
			"$repo_root/os/mm/mm_heap/$source.c" \
			-o "$temp_dir/fatal-$source.o"
	done
	cc -std=c11 -Wall -Wextra -Werror -pthread -I"$fatal_stub" -I"$stub" \
		-I"$repo_root/os/kernel/debug" -c \
		"$repo_root/os/kernel/debug/mem_leak_checker_lifecycle.c" \
		-o "$temp_dir/fatal-lifecycle.o"
	cc -std=c11 -Wall -Wextra -Werror -pthread -I"$fatal_stub" -I"$stub" \
		-I"$repo_root/os/kernel/debug" -c \
		"$repo_root/os/kernel/debug/mem_leak_checker_budget.c" \
		-o "$temp_dir/fatal-budget.o"
	cc -std=c11 -Wall -Wextra -Werror -pthread -I"$stub" \
		-I"$repo_root/os/kernel/debug" -c \
		"$repo_root/os/kernel/debug/mem_leak_checker_domain.c" \
		-o "$temp_dir/fatal-guard.o"
	cc -std=c11 -Wall -Wextra -Werror -pthread -I"$stub" \
		-I"$repo_root/os/kernel/debug" -c \
		"$repo_root/os/kernel/debug/mem_leak_checker_pause_owner.c" \
		-o "$temp_dir/fatal-pause-owner.o"
	cc -std=c11 -Wall -Wextra -Werror -pthread -I"$fatal_stub" -I"$stub" \
		-I"$repo_root/os/kernel/debug" -c \
		"$repo_root/tools/mem_leak_checker_task6_fatal_test.c" \
		-o "$temp_dir/fatal-test.o"
	cc -pthread "$temp_dir"/fatal-*.o -o "$temp_dir/fatal-test"
	"$temp_dir/fatal-test"
}


case "$mode" in
	host) [ "$#" -eq 4 ] || exit 64; run_host_contracts "$4" ;;
	fatal) [ "$#" -eq 3 ] || exit 64; run_fatal_contracts ;;
	*) exit 64 ;;
esac
