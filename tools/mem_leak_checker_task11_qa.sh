#!/bin/bash -p
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mode=${1:?usage: mem_leak_checker_task11_qa.sh host|static}
build=$(mktemp /tmp/mlc-task11-model.XXXXXX)
multiheap_build=$(mktemp /tmp/mlc-task11-multiheap.XXXXXX)
trap 'unlink "$build" "$multiheap_build" 2>/dev/null || true' EXIT HUP INT TERM

case "$mode" in
host)
	cc -std=c11 -Wall -Wextra -Werror -Wno-gnu-include-next -pedantic -pthread \
		-I"$root/os/kernel/debug" -I"$root/tools/mem_leak_checker_task6_stubs" \
		"$root/tools/tests/mem_leak_checker_unified_model.c" \
		"$root/os/kernel/debug/mem_leak_checker_unified.c" \
		"$root/os/kernel/debug/mem_leak_checker_core.c" \
		"$root/os/kernel/debug/mem_leak_checker_index.c" \
		"$root/os/kernel/debug/mem_leak_checker_graph.c" \
		"$root/os/kernel/debug/mem_leak_checker_graph_validate.c" \
		"$root/os/kernel/debug/mem_leak_checker_tarjan.c" \
		"$root/tools/tests/mem_leak_checker_clock_stub.c" \
		"$root/os/kernel/debug/mem_leak_checker_budget.c" -o "$build"
	fixtures=${2:-mlc_production_snapshot,mlc_empty_production_snapshot,mlc_ambiguous_root,mlc_admission_workspace_teardown_race,mlc_production_adapter_faults,mlc_late_failure_atomic_publish,mlc_post_unpin_poison,mlc_teardown_failure_admission}
	repeat=${3:-1}
	case "$repeat" in *[!0-9]*|'') exit 64 ;; esac
	[ "$repeat" -gt 0 ] || exit 64
	old_ifs=$IFS
	IFS=,
	read -r -a fixture_list <<<"$fixtures"
	IFS=$old_ifs
	iteration=1
	while [ "$iteration" -le "$repeat" ]; do
		for fixture in "${fixture_list[@]}"; do
			"$build" "$fixture"
		done
		iteration=$((iteration + 1))
	done
	cc -std=c11 -Wall -Wextra -Werror -pedantic \
		"$root/tools/tests/mem_leak_checker_multiheap_model.c" \
		-o "$multiheap_build"
	"$multiheap_build"
	;;
static)
	python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
source = (root / "os/kernel/debug/mem_leak_checker.c").read_text()
kconfig = (root / "apps/system/mem_leak_checker/Kconfig").read_text()
assert "mlc_unified_analyze" in source
assert source.index("run_mem_leak_checker_owned") < source.index("mlc_domain_guard_release")
assert source.index("notify_test_observer") < source.index("mlc_domain_guard_release")
assert source.index("print_heap_report") < source.index("mlc_lifecycle_complete")
assert "g_candidate_sources" in source and "g_candidate_leak_flags" in source
assert "MLC_NODE_AMBIGUOUS" in source and "MLC_REPORT_AMBIGUOUS" in source
unified = (root / "os/kernel/debug/mem_leak_checker_unified.c").read_text()
assert "root_count > 0 && roots == NULL" in unified
assert "MLC_REPORT_ROW_CAPACITY_FACTOR 2u" in source
assert "row_capacity *= MLC_REPORT_ROW_CAPACITY_FACTOR;" in source
assert "#define MLC_SNAPSHOT_FIXED_EXCLUSION_CAPACITY 32u" in source
assert "static int mlc_exclude_report_buffers(" in source
assert "const struct mlc_invocation_report_s *report" in source
collector_start = source.index("static int mlc_collect_locked_candidates(")
collector_end = source.index("\n}\n\nstatic enum mlc_incomplete_reason_e", collector_start)
assert "mlc_exclude_report_buffers(report" in source[collector_start:collector_end]
report_start = source.index("static int report_init(")
report_end = source.index("\n}\n\nstatic int hash_init", report_start)
assert "memset(report->rows, 0, row_capacity * sizeof(*report->rows));" in source[report_start:report_end]
analysis_start = source.index("static int analyze_unified_snapshot(")
analysis_end = source.index("\n}\n\nstatic struct alloc_node_info_s", analysis_start)
assert "g_candidate_snapshot_count == 0" not in source[analysis_start:analysis_end]
ram_start = source.index("static int ram_check(")
ram_end = source.index("\n}\n\nstatic void print_mem_hex_dump", ram_start)
ram_body = source[ram_start:ram_end]
candidate_start = ram_body.index("if (g_candidate_snapshot_active)")
candidate_end = ram_body.index("} else {", candidate_start)
assert "return heap_check(heap, checker_pid, leak_cnt);" in ram_body[candidate_start:candidate_end]
capture_start = source.index("static int capture_heap_owned(")
capture_end = source.index("\n}\n\nint run_mem_leak_checker", capture_start)
capture_body = source[capture_start:capture_end]
assert "mlc_lifecycle_fail(lifecycle, MLC_INCOMPLETE_TASK_CONTEXT);" in capture_body
assert "mlc_lifecycle_fail(lifecycle, MLC_INCOMPLETE_CAPACITY);" in capture_body
single_start = source.index("int run_mem_leak_checker(")
single_end = source.index("\n}\n\nint run_all_mem_leak_checker_with_capture", single_start)
single_body = source[single_start:single_end]
all_start = source.index("int run_all_mem_leak_checker_with_capture(")
all_body = source[all_start:]
assert "report_init(&lifecycle, &report, CONFIG_KMM_NHEAPS)" in single_body
assert "for (report_index = 1; report_index < CONFIG_KMM_NHEAPS;" in single_body
assert "&kmm_get_baseheap()[report_index]" in single_body
assert "size_t heap_capacity = CONFIG_KMM_NHEAPS;" in all_body
assert "heap_capacity += MLC_DOMAIN_PIN_CAPACITY;" in all_body
assert "for (report_index = 1; report_index < CONFIG_KMM_NHEAPS;" in all_body
assert "report_index = CONFIG_KMM_NHEAPS; report_index < report.heap_count" in all_body
assert "app_report_index = CONFIG_KMM_NHEAPS;" in all_body
assert "guard.pins[report_index].heap != NULL" in all_body
max_alloc = kconfig[kconfig.index("config MEM_LEAK_CHECKER_MAX_ALLOC_COUNT"):kconfig.index("config MEM_LEAK_REMOTE_PAUSED_MAX_POLLS")]
assert "range 1 65536" in max_alloc
print("MLC_TASK11_STATIC status=PASS unified_entry=true copy_before_release=true teardown_after_emit=true")
PY
	;;
*) exit 64 ;;
esac
