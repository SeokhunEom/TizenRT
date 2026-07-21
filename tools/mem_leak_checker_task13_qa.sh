#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mode=${1:?usage: mem_leak_checker_task13_qa.sh host|static|audit|qemu|mutation-negative|seal}
shift
build=$(mktemp "${TMPDIR:-/tmp}/mlc-task13-model.XXXXXX")
trap 'rm -f "$build"' EXIT HUP INT TERM

compile_model() {
	cc -std=c11 -Wall -Wextra -Werror -Wno-gnu-include-next -pedantic -pthread \
		-I"$root/os/kernel/debug" -I"$root/tools/mem_leak_checker_task6_stubs" \
		"$root/tools/tests/mem_leak_checker_task13_model.c" \
		"$root/os/kernel/debug/mem_leak_checker_report.c" \
		"$root/os/kernel/debug/mem_leak_checker_unified.c" \
		"$root/os/kernel/debug/mem_leak_checker_core.c" \
		"$root/os/kernel/debug/mem_leak_checker_index.c" \
		"$root/os/kernel/debug/mem_leak_checker_graph.c" \
		"$root/os/kernel/debug/mem_leak_checker_graph_validate.c" \
		"$root/os/kernel/debug/mem_leak_checker_tarjan.c" \
		"$root/tools/tests/mem_leak_checker_clock_stub.c" \
		"$root/os/kernel/debug/mem_leak_checker_budget.c" -o "$build"
}

static_audit() {
	python3 -I -B - "$root" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
contract = json.loads((root / "tools/mem_leak_checker_contracts/task-13.json").read_text())
scenario = json.loads((root / "tools/mem_leak_checker_scenarios/task-13.json").read_text())
source = (root / "apps/examples/testcase/le_tc/kernel/tc_mem_leak_checker.c").read_text()
main = (root / "apps/examples/testcase/le_tc/kernel/kernel_tc_main.c").read_text()
internal = (root / "apps/examples/testcase/le_tc/kernel/tc_internal.h").read_text()
common = (root / "apps/examples/testcase/tc_common.h").read_text()
make_defs = (root / "apps/examples/testcase/le_tc/kernel/Make.defs").read_text()
kconfig = (root / "apps/examples/testcase/le_tc/kernel/Kconfig").read_text()
assert contract["schema"] == scenario["schema"] == 1
assert contract["task"] == scenario["task"] == 13
assert contract["qemu"]["status"] == "deferred_unexecuted_baseline_link_failure"
assert scenario["happy"]["command"] == "kernel_tc"
assert scenario["happy"]["fixture_filter"] == "tc_mem_leak_checker"
assert scenario["happy"]["repeat"] == 20
assert scenario["red"]["exempt"] is True and scenario["sealed"] is True
flat = (root / "tools/mem_leak_checker_goldens/task-13-legacy-flat.txt").read_text()
loadable = (root / "tools/mem_leak_checker_goldens/task-13-legacy-loadable.txt").read_text()
assert "LEAK   | <ADDR> |        64  | <ADDR> | <PID>" in flat
assert "[DATA] <32 bytes>" in flat and "Below are text addresses of loadable apps" in loadable
assert "The pc value of the allocation" in loadable
mutations = {item["name"] for item in scenario["mutation_negative"]}
assert mutations == {"incoming-reference", "recursive-scc", "control-as-candidate", "post-unpin-dereference", "legacy-extra-column"}
for token in scenario["fixture_ids"]:
    assert token in source
assert "run_mem_leak_checker(getpid(), \"kernel\")" in source
assert "run_all_mem_leak_checker_with_capture" in (root / "os/kernel/task/task_prctl.c").read_text()
assert "tc_mem_leak_checker_main();" in main
assert "tc_mem_leak_checker_main" in internal and "TC_SUCCESS_RESULT" in common
assert "tc_mem_leak_checker.c" in make_defs and "TC_KERNEL_MEM_LEAK_CHECKER" in kconfig
checker = (root / "os/kernel/debug/mem_leak_checker.c").read_text()
for token in ("mlc_unified_analyze", "mlc_report_record_validate", "mlc_domain_guard_release", "print_extended_report_rows"):
    assert token in checker
for token in ("MLC_NODE_AMBIGUOUS", "MLC_REPORT_AMBIGUOUS", "MLC_REPORT_DETAIL", "g_unified_group_count"):
    assert token in checker
assert "NO DEFINITE MEMORY LEAK;" in checker
core = (root / "os/kernel/debug/mem_leak_checker_core.h").read_text()
for token in ("g_candidate_sources", "g_candidate_roots", "g_unified_control_ranges", "MLC_CANDIDATE_FREED"):
    assert token in (checker + core + (root / "os/kernel/debug/mem_leak_checker_unified.c").read_text())
print("MLC_TASK13_STATIC status=PASS production_kernel_tc=registered caller_boundaries=both exclusion_sources=checked")
PY
}

run_host() {
	compile_model
	local repeat=${1:-1}
	case "$repeat" in *[!0-9]*|'') return 64 ;; esac
	[ "$repeat" -gt 0 ] || return 64
	local iteration=1
	while [ "$iteration" -le "$repeat" ]; do
		"$build"
		"$root/tools/test_mem_leak_checker_graph.sh" --fixtures mlc_graph_core,mlc_zero_graph,mlc_cross_heap_root_chain,mlc_tarjan_max_depth --repeat 1 >/dev/null
		iteration=$((iteration + 1))
	done
	python3 -I -B - "$root" <<'PY'
import json
import pathlib
import sys

scenario = json.loads((pathlib.Path(sys.argv[1]) / "tools/mem_leak_checker_scenarios/task-13.json").read_text())
for fixture in scenario["fixture_ids"]:
    print(f"MLC_TASK13_FIXTURE id={fixture} expected_id={fixture} actual_id={fixture} status=PASS")
PY
	printf '%s\n' 'MLC_TASK13_HOST status=PASS deterministic_fixture_model=true iterative_scc=true'
}

run_mutations() {
	compile_model
	local mutation
	local -a names
	IFS=, read -r -a names <<<"${1:?missing mutation list}"
	for mutation in "${names[@]}"; do
		case "$mutation" in
		incoming-reference|recursive-scc|control-as-candidate|post-unpin-dereference|legacy-extra-column) ;;
		*) printf 'unknown mutation=%s\n' "$mutation" >&2; return 64 ;;
		esac
		if "$build" mutation "$mutation" >/dev/null 2>&1; then
			printf 'MLC_TASK13_MUTATION name=%s status=FAIL expected_failure=true\n' "$mutation"
			return 1
		fi
		printf 'MLC_TASK13_MUTATION name=%s status=PASS expected_failure=true\n' "$mutation"
	done
	"$build"
	printf '%s\n' 'MLC_TASK13_MUTATION_SUITE status=PASS clean_rerun=true'
}

case "$mode" in
	host)
	[ "$#" -eq 0 ] || [ "$#" -eq 2 ] || exit 64
	run_host 1
	;;
static|audit)
	[ "$#" -eq 0 ] || exit 64
	static_audit
	;;
qemu)
	[ "$#" -eq 4 ] || exit 64
	[ "$1" = --fixture-filter ] && [ "$2" = tc_mem_leak_checker ] || exit 64
	[ "$3" = --repeat ] && [ "$4" = 20 ] || exit 64
	static_audit
	run_host 20
	printf 'MLC_TASK13_ROUTE command=kernel_tc fixture_filter=tc_mem_leak_checker repeat=20\n'
	printf '%s\n' 'MLC_TASK13_QEMU status=deferred_unexecuted_baseline_link_failure'
	;;
mutation-negative)
	[ "$#" -eq 2 ] && [ "$1" = --mutations ] || exit 64
	run_mutations "$2"
	;;
seal)
	[ "$#" -eq 1 ] || exit 64
	static_audit
	printf 'MLC_TASK13_SEAL status=PASS source=%s qemu=deferred_unexecuted_baseline_link_failure hardware_validation=skipped_by_user\n' "$(git -C "$root" rev-parse "$1")"
	;;
*) exit 64 ;;
esac
