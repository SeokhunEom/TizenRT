#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mode=qa
if [ "${1:-}" = --red ]; then
	mode=red
	shift
fi
artifact_dir=${1:?usage: mem_leak_checker_task5_qa.sh [--red] ARTIFACT_DIR}
mkdir -p "$artifact_dir"

cc_bin=${CC:-cc}
model_bin="/tmp/mlc-todo5-model-$$"
lifecycle_bin="/tmp/mlc-todo5-lifecycle-$$"
fallback_obj="/tmp/mlc-todo5-fallback-$$.o"
irqcount_obj="/tmp/mlc-todo5-irqcount-$$.o"
irq_smp_bin="/tmp/mlc-todo5-irq-smp-$$"
irq_up_bin="/tmp/mlc-todo5-irq-up-$$"
irq_fallback_bin="/tmp/mlc-todo5-irq-fallback-$$"
negative_log="$artifact_dir/smp-no-irqcount.log"
trap 'rm -f "$model_bin" "$lifecycle_bin" "$fallback_obj" "$irqcount_obj" "$irq_smp_bin" "$irq_up_bin" "$irq_fallback_bin"' EXIT HUP INT TERM

if [ "$mode" = red ]; then
	"$cc_bin" -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
		-DMLC_TASK5_RED_PROOF \
		"$repo_root/tools/mem_leak_checker_task5_model.c" -o "$model_bin"
	set +e
	"$model_bin" | tee "$artifact_dir/red.log"
	red_exit=${PIPESTATUS[0]}
	set -e
	[ "$red_exit" -eq 86 ] || exit 1
	exit 86
fi

"$cc_bin" -std=c11 -Wall -Wextra -Werror -pthread \
	"$repo_root/tools/mem_leak_checker_task5_model.c" -o "$model_bin"
"$model_bin" | tee "$artifact_dir/model.log"

"$cc_bin" -std=c11 -Wall -Wextra -Werror -pthread \
	-I"$repo_root/tools/mem_leak_checker_task5_stubs" \
	-I"$repo_root/os/kernel/debug" \
	"$repo_root/os/kernel/debug/mem_leak_checker_lifecycle.c" \
	"$repo_root/os/kernel/debug/mem_leak_checker_budget.c" \
	"$repo_root/tools/mem_leak_checker_task5_lifecycle_test.c" \
	-o "$lifecycle_bin"
"$lifecycle_bin" | tee "$artifact_dir/lifecycle.log"

"$cc_bin" -std=c11 -Wall -Wextra -Werror \
	-DCONFIG_IRQCOUNT -DCONFIG_SMP -DCONFIG_SMP_NCPUS=2 \
	-DCONFIG_SCHED_INSTRUMENTATION_SPINLOCKS \
	-DCONFIG_SCHED_INSTRUMENTATION_CSECTION \
	-I"$repo_root/tools/mem_leak_checker_task5_stubs" \
	"$repo_root/os/kernel/irq/irq_csection.c" \
	"$repo_root/tools/mem_leak_checker_task5_irq_actual_test.c" \
	-o "$irq_smp_bin"
"$irq_smp_bin" | tee "$artifact_dir/irq-smp-actual.log"

"$cc_bin" -std=c11 -Wall -Wextra -Werror \
	-DCONFIG_IRQCOUNT -DCONFIG_SCHED_INSTRUMENTATION_SPINLOCKS \
	-DCONFIG_SCHED_INSTRUMENTATION_CSECTION \
	-I"$repo_root/tools/mem_leak_checker_task5_stubs" \
	"$repo_root/os/kernel/irq/irq_csection.c" \
	"$repo_root/tools/mem_leak_checker_task5_irq_actual_test.c" \
	-o "$irq_up_bin"
"$irq_up_bin" | tee "$artifact_dir/irq-up-actual.log"

"$cc_bin" -std=c11 -Wall -Wextra -Werror -DCONFIG_ARCH_CORTEXM3 \
	-I"$repo_root/tools/mem_leak_checker_task5_stubs" \
	"$repo_root/os/kernel/irq/irq_trycritical.c" \
	"$repo_root/tools/mem_leak_checker_task5_irq_fallback_test.c" \
	-o "$irq_fallback_bin"
"$irq_fallback_bin" | tee "$artifact_dir/irq-fallback-actual.log"

"$cc_bin" -std=c11 -Wall -Wextra -Werror -DCONFIG_ARCH_CORTEXM3 \
	-I"$repo_root/tools/mem_leak_checker_task5_stubs" \
	-c "$repo_root/os/kernel/irq/irq_trycritical.c" -o "$fallback_obj"
"$cc_bin" -std=c11 -Wall -Wextra -Werror -DCONFIG_IRQCOUNT \
	-I"$repo_root/tools/mem_leak_checker_task5_stubs" \
	-c "$repo_root/os/kernel/irq/irq_trycritical.c" -o "$irqcount_obj"
test "$(nm "$fallback_obj" | awk '$NF ~ /^_?irq_try_enter_critical_fresh$/ { count++ } END { print count + 0 }')" -eq 1
test "$(nm "$irqcount_obj" | awk '$NF ~ /^_?irq_try_enter_critical_fresh$/ { count++ } END { print count + 0 }')" -eq 0
if "$cc_bin" -std=c11 -DCONFIG_SMP \
	-I"$repo_root/tools/mem_leak_checker_task5_stubs" \
	-c "$repo_root/os/kernel/irq/irq_trycritical.c" -o /dev/null \
	>"$negative_log" 2>&1; then
	printf '%s\n' "SMP without IRQCOUNT unexpectedly compiled" >&2
	exit 1
fi
grep -q "CONFIG_SMP requires CONFIG_IRQCOUNT" "$negative_log"
printf '%s\n' "MLC_TASK5_VARIANT_COMPILE status=PASS fallback=up_no_irqcount negative=smp_no_irqcount"

python3 - "$repo_root" <<'PY' | tee "$artifact_dir/static.log"
from pathlib import Path
import sys

root = Path(sys.argv[1])
csection = (root / "os/kernel/irq/irq_csection.c").read_text()
fallback = (root / "os/kernel/irq/irq_trycritical.c").read_text()
make_defs = (root / "os/kernel/irq/Make.defs").read_text()
public = (root / "os/include/tinyara/irq.h").read_text()
arm_m = (root / "os/arch/arm/include/armv7-m/irq.h").read_text()
arm_a = (root / "os/arch/arm/include/armv7-a/irq.h").read_text()
checker = (root / "os/kernel/debug/mem_leak_checker.c").read_text()
domain = (root / "os/kernel/debug/mem_leak_checker_domain.c").read_text()
lifecycle = (root / "os/kernel/debug/mem_leak_checker_lifecycle.c").read_text()
lifecycle_header = (root / "os/kernel/debug/mem_leak_checker_lifecycle.h").read_text()

assert public.count("int irq_try_enter_critical_fresh(irqstate_t *flags);") == 1
assert "CSRCS += irq_trycritical.c" in make_defs
assert "#error CONFIG_SMP requires CONFIG_IRQCOUNT" in fallback
assert fallback.count("irq_try_enter_critical_fresh") == 1
assert csection.count("int irq_try_enter_critical_fresh") == 1
fresh = csection[csection.index("int irq_try_enter_critical_fresh"):]
fresh = fresh[:fresh.index("\n}") + 2]
assert fresh.count("spin_trylock_wo_note") == 2
assert "irq_waitlock" not in fresh and "spin_setbit" not in fresh and "spin_lock(" not in fresh
assert fresh.index("&g_cpu_irqlock") < fresh.index("&g_cpu_irqsetlock")
assert "spin_unlock_wo_note(&g_cpu_irqlock)" in fresh
assert fresh.index("saved = irqsave();") < fresh.index("int cpu = this_cpu();")
assert fresh.index("int cpu = this_cpu();") < fresh.index("rtcb = current_task(cpu);")
assert "up_irq_saved_enabled(saved)" in fresh and "irqstate()" not in fresh
assert "up_irq_saved_enabled" in arm_m and "up_irq_saved_enabled" in arm_a
assert "up_testset(&g_mem_leak_checker_admission)" in lifecycle
assert "mlc_lifecycle_unwind_to" in checker + domain
assert "mlc_budget_consume" in lifecycle and "mlc_budget_request_resume" in lifecycle
assert lifecycle_header.index("MLC_PHASE_DOMAIN") < lifecycle_header.index("MLC_PHASE_CRITICAL")
assert lifecycle_header.index("MLC_PHASE_CRITICAL") < lifecycle_header.index("MLC_PHASE_HEAPS")
assert "terminal_ledger[MLC_LEDGER_CAPACITY]" in lifecycle_header
assert "mlc_lifecycle_store_provisional" in lifecycle
assert "static int capture_info" in checker and "static void print_heap_report" in checker
capture = checker[checker.index("static int capture_info"):checker.index("static void print_heap_report")]
assert "printf(" not in capture and "mlc_lifecycle_store_provisional" in capture
owned = checker[checker.index("static int run_mem_leak_checker_owned"):checker.index("int run_mem_leak_checker(")]
assert "printf(" not in owned
all_owned = checker[checker.index("int run_all_mem_leak_checker"):]
assert all_owned.index("report_init") < all_owned.index("run_mem_leak_checker_owned")
assert all_owned.rindex("run_mem_leak_checker_owned") < all_owned.index('printf("\\nKernel :\\n")')
print("MLC_TASK5_STATIC status=PASS variants=3 negative=smp_no_irqcount atomic_report=true phase_order=true")
PY
