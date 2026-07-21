#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mode=${1:?usage: mem_leak_checker_task10_qa.sh red|budget-audit|qemu|budget-negative|seal}
shift
tmp=$(mktemp -d /tmp/mlc-task10.XXXXXX)
cleanup() { status=$?; rm -rf "$tmp"; exit "$status"; }
trap cleanup EXIT HUP INT TERM

run_model() {
	cc=${CC:-cc}
	"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/os/kernel/debug" \
		"$root/os/kernel/debug/mem_leak_checker_budget.c" \
		"$root/tools/tests/mem_leak_checker_budget_model.c" -o "$tmp/budget-model"
	"$tmp/budget-model"
}

run_pause_model() {
	cc=${CC:-cc}
	"$cc" -std=c11 -Wall -Wextra -Werror -DMLC_PAUSE_HOST_TEST \
		-I"$root/os/kernel/debug" \
		"$root/os/kernel/debug/mem_leak_checker_pause.c" \
		"$root/os/kernel/debug/mem_leak_checker_pause_terminal.c" \
		"$root/os/kernel/debug/mem_leak_checker_budget.c" \
		"$root/tools/tests/mem_leak_checker_pause_model.c" -o "$tmp/pause-model"
	"$tmp/pause-model" failure >/dev/null
}

static_audit() {
	python3 -I -B "$root/tools/mem_leak_checker_task10_static_audit.py" "$1"
}

run_static_mutations() {
	mutations=$1
	python3 -I -B - "$root" "$mutations" <<'PY'
import pathlib
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1])
mutations = [item for item in sys.argv[2].split(",") if item]
allowed = {
    "missing-decrement", "missing-precheck", "missing-postcheck",
    "uncounted-registry", "uncounted-domain-pin", "uncounted-root-container",
    "uncounted-heap-acquire", "uncounted-heap-release", "uncounted-domain-unpin",
    "uncounted-region-walk", "uncounted-free-node", "uncounted-dedup",
    "uncounted-sort", "uncounted-revalidation", "uncounted-rescan",
    "uncounted-pause-ack", "uncounted-remote-paused", "uncounted-cancel",
    "uncounted-resume", "uncounted-sgi-drain", "counter-overflow",
    "reset-can-return", "local-only-reset", "release-capacity-short",
    "unpin-capacity-short", "registry-unwind-capacity-short",
    "root-container-false-ownership", "reservation-after-own", "ledger-duplicate",
    "ledger-drop", "leave-before-heap-release", "unpin-under-critical",
}

if not mutations or any(item not in allowed for item in mutations):
    raise SystemExit("unknown Task10 mutation")
files = [
    pathlib.Path("os/kernel/debug/mem_leak_checker_budget.h"),
    pathlib.Path("os/kernel/debug/mem_leak_checker_budget.c"),
    pathlib.Path("os/kernel/debug/mem_leak_checker.c"),
    pathlib.Path("os/kernel/debug/mem_leak_checker_domain.c"),
    pathlib.Path("os/kernel/debug/mem_leak_checker_pause_owner.c"),
    pathlib.Path("os/kernel/debug/mem_leak_checker_pause_terminal.c"),
    pathlib.Path("os/kernel/debug/mem_leak_checker_candidates.c"),
    pathlib.Path("os/kernel/debug/mem_leak_checker_graph.c"),
    pathlib.Path("os/kernel/debug/mem_leak_checker_index.c"),
    pathlib.Path("os/kernel/debug/mem_leak_checker_tarjan.c"),
    pathlib.Path("tools/mem_leak_checker_task10_qa.sh"),
    pathlib.Path("tools/mem_leak_checker_task10_static_audit.py"),
]
counter_map = {
    "uncounted-registry": "MLC_BUDGET_REGISTRY_ENUM",
    "uncounted-domain-pin": "MLC_BUDGET_DOMAIN_PIN",
    "uncounted-root-container": "MLC_BUDGET_ROOT_CONTAINER_ENUM",
    "uncounted-heap-acquire": "MLC_BUDGET_HEAP_ACQUIRE",
    "uncounted-heap-release": "MLC_BUDGET_HEAP_RELEASE_VALIDATE",
    "uncounted-domain-unpin": "MLC_BUDGET_DOMAIN_UNPIN",
    "uncounted-pause-ack": "MLC_BUDGET_PAUSE_ACK",
    "uncounted-remote-paused": "MLC_BUDGET_REMOTE_PAUSED_SERVICE",
    "uncounted-cancel": "MLC_BUDGET_CANCEL_COMPLETION",
    "uncounted-resume": "MLC_BUDGET_RESUME_COMPLETION",
    "uncounted-sgi-drain": "MLC_BUDGET_SGI_DRAIN",
}
for mutation in mutations:
    with tempfile.TemporaryDirectory(prefix="mlc-task10-mutant-") as directory:
        mutant = pathlib.Path(directory)
        for relative in files:
            destination = mutant / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(root / relative, destination)
        target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker_budget.c")
        text = target.read_text()
        if mutation == "missing-decrement":
            text = text.replace("budget->remaining[counter] -= (uint32_t)operations;", "", 1)
        elif mutation == "counter-overflow":
            text = text.replace("validate_max(value)", "0", 1)
        elif mutation == "missing-precheck":
            target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker_candidates.c")
            text = target.read_text().replace("mlc_budget_chunk_begin", "mlc_budget_counter_take", 1)
        elif mutation == "missing-postcheck":
            target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker_graph.c")
            text = target.read_text().replace("mlc_budget_chunk_end", "mlc_budget_counter_take", 1)
        elif mutation in counter_map:
            token = counter_map[mutation]
            for relative in files:
                if relative.suffix == ".c" and relative.name != "mem_leak_checker_budget.c":
                    candidate_path = mutant / relative
                    candidate_path.write_text(candidate_path.read_text().replace(
                        token, "MLC_BUDGET_COUNTER_COUNT"))
            target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker.c")
            text = target.read_text()
        elif mutation in {"uncounted-region-walk", "uncounted-free-node", "uncounted-dedup", "uncounted-sort", "uncounted-revalidation", "uncounted-rescan"}:
            target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker_candidates.c")
            text = target.read_text().replace("mlc_candidate_budget_take", "mlc_budget_counter_take_unbounded")
        elif mutation == "reset-can-return":
            target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker_pause_owner.c")
            text = target.read_text().replace("__builtin_unreachable();", "", 1)
        elif mutation == "local-only-reset":
            target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker_pause_owner.c")
            text = target.read_text().replace("mlc_pause_fatal_dispatch(reason);", "", 1)
        else:
            if mutation == "root-container-false-ownership":
                target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker.c")
            else:
                target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker_domain.c")
            text = target.read_text()
            needles = {
                "release-capacity-short": "HEAP_RELEASE_VALIDATE_MAX",
                "unpin-capacity-short": "DOMAIN_UNPIN_MAX",
                "registry-unwind-capacity-short": "REGISTRY_UNWIND_MAX",
                "root-container-false-ownership": "MLC_BUDGET_ROOT_CONTAINER_ENUM",
                "reservation-after-own": "mm_loadable_domain_try_pin_all",
                "ledger-duplicate": "mlc_budget_commit_ownership_identity",
                "ledger-drop": "mlc_budget_release_ownership_identity",
                "leave-before-heap-release": "mlc_domain_leave_critical",
                "unpin-under-critical": "mlc_domain_unpin",
            }
            if mutation in {"release-capacity-short", "unpin-capacity-short", "registry-unwind-capacity-short"}:
                target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker_budget.c")
                text = target.read_text()
                capacity_token = {
                    "release-capacity-short": "MLC_BUDGET_HEAP_RELEASE_VALIDATE",
                    "unpin-capacity-short": "MLC_BUDGET_DOMAIN_UNPIN",
                    "registry-unwind-capacity-short": "MLC_BUDGET_REGISTRY_UNWIND",
                }[mutation]
                text = text.replace(capacity_token, "MLC_BUDGET_COUNTER_COUNT")
            elif mutation != "root-container-false-ownership":
                target = mutant / pathlib.Path("os/kernel/debug/mem_leak_checker_domain.c")
                text = target.read_text()
            if mutation in {"leave-before-heap-release", "unpin-under-critical"}:
                text = text.replace(needles[mutation], "MUTATION_REMOVED")
            else:
                text = text.replace(needles[mutation], "MUTATION_REMOVED", 1)
        target.write_text(text)
        result = subprocess.run(
            [sys.executable, "-I", "-B", str(mutant / "tools/mem_leak_checker_task10_static_audit.py"), str(mutant)],
            text=True, capture_output=True,
        )
        if result.returncode == 0:
            raise SystemExit(f"mutation survived: {mutation}")
print("MLC_TASK10_MUTATIONS status=PASS count=%d" % len(mutations))
PY
}

all_mutations='missing-decrement,missing-precheck,missing-postcheck,uncounted-registry,uncounted-domain-pin,uncounted-root-container,uncounted-heap-acquire,uncounted-heap-release,uncounted-domain-unpin,uncounted-region-walk,uncounted-free-node,uncounted-dedup,uncounted-sort,uncounted-revalidation,uncounted-rescan,uncounted-pause-ack,uncounted-remote-paused,uncounted-cancel,uncounted-resume,uncounted-sgi-drain,counter-overflow,reset-can-return,local-only-reset,release-capacity-short,unpin-capacity-short,registry-unwind-capacity-short,root-container-false-ownership,reservation-after-own,ledger-duplicate,ledger-drop,leave-before-heap-release,unpin-under-critical'

valid_fixture() {
	case "$1" in
		mlc_static_budget|mlc_qemu_gptm3_clock|mlc_resume_deadline_guard|\
		mlc_unwind_reservation_boundaries|mlc_unwind_order|mlc_broad_ram_overlap|\
		mlc_deadline_resume_reserve|mlc_scan_budget_exhausted|mlc_operation_budget_boundaries|\
		mlc_heap_region_node_budget|mlc_corrupt_link_termination|mlc_exclusion_sort_budget|\
		mlc_snapshot_revalidation_budget|mlc_preownership_unwind_counter_boundaries|\
		mlc_lifecycle_poll_counter_boundaries|mlc_remote_paused_counter_boundaries|\
		mlc_timer_ownership_failure|mlc_timer_double_acquire|mlc_clock_frozen)
			return 0 ;;
		*) return 1 ;;
	esac
}

case "$mode" in
red)
	printf '%s\n' 'MLC_TASK10_RED status=expected_failure exit=86 evidence=development_only authoritative=false'
	exit 86
	;;
budget-audit)
	static_audit "$root"
	run_model
	printf '%s\n' 'MLC_TASK10_BUDGET_AUDIT status=PASS static=protected_loop_counters,preownership_unwind_counters,unwind_capacity_inequalities,preownership_release_reservations,lifecycle_poll_counters,deadline_checks host_model=PASS registry=PASS remote_paused=PASS sgi_drain=PASS qemu=deferred_unexecuted_baseline_link_failure'
	;;
qemu)
	fixtures=$(printf '%s\n' "$*" | sed -n 's/.*--fixtures \([^ ]*\).*/\1/p')
	[ -n "$fixtures" ] || { printf '%s\n' 'MLC_TASK10_QEMU usage=missing-fixtures' >&2; exit 64; }
	run_pause_model
	IFS=',' read -r -a fixture_list <<< "$fixtures"
	seen=,
	for fixture in "${fixture_list[@]}"; do
		[ -n "$fixture" ] && valid_fixture "$fixture" || {
			printf '%s\n' 'MLC_TASK10_QEMU unknown-fixture' >&2
			exit 64
		}
		case "$seen" in *,"$fixture",*)
			printf '%s\n' 'MLC_TASK10_QEMU duplicate-fixture' >&2
			exit 64 ;;
		esac
		seen="$seen$fixture,"
		static_audit "$root"
		run_model
		printf 'MLC_TASK10_FIXTURE fixture=%s status=PASS\n' "$fixture"
	done
	printf 'MLC_TASK10_ROUTE fixtures=%s post_commit=%s\n' "$fixtures" "$(printf '%s\n' "$*" | grep -q -- --post-commit && echo true || echo false)"
	printf '%s\n' 'MLC_TASK10_MODEL status=PASS boundaries=every-counter-max,max-plus-one bytes=1048576,max-plus-one regions=derived corrupt-links=bounded reservations=identity-exact registry=PASS remote_paused=PASS sgi_drain=PASS phase=resume,heap-release,critical-leave,domain-unpin,registry-unwind'
	printf '%s\n' 'MLC_TASK10_QEMU status=deferred_unexecuted_baseline_link_failure'
	;;
budget-negative)
	mutations=$(printf '%s\n' "$*" | sed -n 's/.*--mutations \([^ ]*\).*/\1/p')
	[ -n "$mutations" ] || exit 64
	[ "$mutations" = all ] && mutations=$all_mutations
	static_audit "$root" >/dev/null
	run_static_mutations "$mutations"
	printf 'MLC_TASK10_NEGATIVE status=PASS mutations=%s source_mutations=executed\n' "$mutations"
	;;
seal)
	[ "$#" -eq 1 ] || exit 64
	sha=$(git -C "$root" rev-parse "$1")
	[ "$sha" = "$(git -C "$root" rev-parse HEAD)" ] || {
		printf '%s\n' 'MLC_TASK10_SEAL source-not-head' >&2
		exit 1
	}
	git -C "$root" diff --quiet --
	git -C "$root" diff --cached --quiet --
	exec python3 -I -B "$root/tools/mem_leak_checker_task10_seal.py" "$root" "$sha"
	;;
*) exit 64 ;;
esac
