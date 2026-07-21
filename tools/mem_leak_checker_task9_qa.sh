#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mode=${1:?usage: mem_leak_checker_task9_qa.sh red|run MODE|seal SOURCE}
shift
tmp=
cleanup() {
	status=$?
	trap - EXIT HUP INT TERM
	case "$tmp" in /tmp/mlc-task9.*|/private/tmp/mlc-task9.*) rm -rf "$tmp" ;; esac
	exit "$status"
}
trap cleanup EXIT HUP INT TERM

if [ "$mode" = red ]; then
	[ ! -e "$root/os/kernel/debug/mem_leak_checker_pause.c" ] || exit 1
	printf '%s\n' 'MLC_TASK9_RED status=expected_failure exit=86 evidence=development_only authoritative=false'
	exit 86
fi

if [ "$mode" = seal ]; then
	[ "$#" -eq 1 ] || exit 64
	source=$1
	receiving_sha=$(git -C "$root" rev-parse "$source")
	[ "$receiving_sha" = "$(git -C "$root" rev-parse HEAD)" ] || exit 1
	git -C "$root" diff --quiet --
	git -C "$root" diff --cached --quiet --
	exec python3 -I -B - "$root" "$receiving_sha" <<'PY'
import hashlib, json, os, pathlib, shlex, subprocess, sys, tempfile
root = pathlib.Path(sys.argv[1])
source_sha = sys.argv[2]
scenario_path = root / "tools/mem_leak_checker_scenarios/task-9.json"
scenario = json.loads(scenario_path.read_text())
results = {}
for bucket in ("happy", "failure", "fatal"):
    entry = scenario[bucket][0]
    with tempfile.TemporaryDirectory(prefix="mlc-task9-seal.") as directory:
        evidence_path = pathlib.Path(directory) / "evidence.json"
        environment = dict(os.environ, MLC_TASK9_EVIDENCE_OUT=str(evidence_path))
        completed = subprocess.run(shlex.split(entry["command"]), cwd=root,
                                   text=True, capture_output=True, env=environment)
        evidence = json.loads(evidence_path.read_text())
    if completed.returncode != entry["expected_exit"] or completed.stderr:
        raise SystemExit(f"task-9 {bucket} command failed")
    records = completed.stdout.splitlines()
    if records != entry["expected_records"]:
        raise SystemExit(f"task-9 {bucket} transcript drift")
    results[bucket] = {
        "command": entry["command"],
        "exit": completed.returncode,
        "records": records,
        "stdout_sha256": hashlib.sha256(completed.stdout.encode()).hexdigest(),
        "build_evidence": evidence,
    }
tree = subprocess.check_output(["/usr/bin/git", "-C", root, "rev-parse", "HEAD^{tree}"], text=True).strip()
receipt = {
    "schema": 1,
    "task": 9,
    "kind": "post-integration",
    "source_sha": source_sha,
    "tree": tree,
    "scenario_sha256": hashlib.sha256(scenario_path.read_bytes()).hexdigest(),
    "results": results,
    "qemu": "deferred_unexecuted_baseline_link_failure",
    "hardware_validation": "skipped_by_user",
    "reset_executed": False,
    "publication": "o_excl_fsync_non_atomic_exfat",
    "cleanup": {"temporary_paths": "removed", "qemu_processes": 0},
}
artifact = root / ".omo/start-work/artifacts/task-9-executor"
artifact.mkdir(parents=True, exist_ok=True)
path = artifact / f"task-9-post-integration-{source_sha}.json"
flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
if hasattr(os, "O_NOFOLLOW"):
    flags |= os.O_NOFOLLOW
fd = os.open(path, flags, 0o600)
try:
    payload = json.dumps(receipt, sort_keys=True, separators=(",", ":")).encode() + b"\n"
    os.write(fd, payload)
    os.fsync(fd)
finally:
    os.close(fd)
directory = os.open(artifact, os.O_RDONLY)
try:
    os.fsync(directory)
finally:
    os.close(directory)
print(f"MLC_TASK9_SEAL status=PASS source={source_sha} receipt={path.relative_to(root)}")
PY
fi

[ "$mode" = run ] && [ "$#" -eq 2 ] || exit 64
kind=$1
fixtures=$2
tmp=$(mktemp -d /tmp/mlc-task9.XXXXXX)
cc=${CC:-cc}
"$cc" -std=c11 -Wall -Wextra -Werror -DMLC_PAUSE_HOST_TEST \
	-I"$root/os/kernel/debug" \
	"$root/os/kernel/debug/mem_leak_checker_pause.c" \
	"$root/os/kernel/debug/mem_leak_checker_pause_terminal.c" \
	"$root/os/kernel/debug/mem_leak_checker_budget.c" \
	"$root/tools/tests/mem_leak_checker_pause_model.c" -o "$tmp/model"
"$tmp/model" "$kind" >/dev/null

printf '%s\n' '#define CONFIG_SMP 1' '#define CONFIG_SMP_NCPUS 2' \
	'#define CONFIG_MEM_LEAK_REMOTE_PAUSED_MAX_POLLS 3' \
	>"$tmp/owner-config.h"

mkdir -p "$tmp/stubs/tinyara" "$tmp/stubs/arch" "$tmp/stubs/sched"
printf '%s\n' '#define CONFIG_SMP 1' '#define CONFIG_SMP_NCPUS 2' \
	'#define CONFIG_MEM_LEAK_REMOTE_PAUSED_MAX_POLLS 65536' \
	>"$tmp/stubs/tinyara/config.h"
printf '%s\n' '#ifndef MLC_TASK9_IRQ_STUB_H' '#define MLC_TASK9_IRQ_STUB_H' \
	'#include <stdbool.h>' '#include <stdint.h>' \
	'typedef uintptr_t irqstate_t;' \
	'static inline bool up_irq_saved_enabled(irqstate_t value) { return value != 0; }' \
	'#endif' \
	>"$tmp/stubs/arch/irq.h"
printf '%s\n' '#include <arch/irq.h>' \
	'#define UP_MEM_LEAK_CAPTURE_MAGIC 0x4d4c4352' \
	'#define UP_MEM_LEAK_CAPTURE_VERSION 1' \
	'#define UP_MEM_LEAK_CAPTURE_WORDS 18' \
	'#define UP_MEM_LEAK_CAPTURE_FLAG_TASK 1' \
	'#define UP_MEM_LEAK_CAPTURE_FLAG_EXCEPTION 2' \
	'#define UP_MEM_LEAK_CAPTURE_FLAG_ARMV7_A 512' \
	'#define UP_MEM_LEAK_CAPTURE_CALLEE_MASK 255' \
	'#define REG_R4 4' '#define REG_SP 13' '#define REG_CPSR 16' \
	'struct up_mem_leak_capture_s { unsigned int magic; unsigned short version; unsigned short words; unsigned int flags; unsigned int callee_saved[8]; unsigned int stack_pointer; unsigned int caller_boundary; unsigned int status; unsigned int exception; unsigned int cpu; unsigned int tcb; unsigned int callee_saved_mask; };' \
	'void up_mem_leak_capture_current(struct up_mem_leak_capture_s *capture);' \
	'int up_mem_leak_pause_request(int cpu);' \
	'unsigned long long up_mem_leak_monotonic_usec(void);' \
	'int this_cpu(void);' >"$tmp/stubs/tinyara/arch.h"
printf '%s\n' '#include <stdint.h>' 'struct tcb_s { int value; };' \
	'struct tcb_s *current_task(int cpu);' >"$tmp/stubs/sched/sched.h"
printf '%s\n' '#define SP_DMB() __atomic_thread_fence(__ATOMIC_SEQ_CST)' \
	'#define SP_DSB() __atomic_thread_fence(__ATOMIC_SEQ_CST)' \
	'#define SP_SEV() do {} while (0)' \
	'#define SP_UNLOCKED 0' '#define SP_LOCKED 1' \
	'typedef int spinlock_t;' \
	'static inline int up_testset(volatile spinlock_t *value) { return __atomic_exchange_n(value, SP_LOCKED, __ATOMIC_ACQ_REL); }' \
	>"$tmp/stubs/tinyara/spinlock.h"
printf '%s\n' '#include <stdint.h>' 'typedef unsigned long clock_t;' \
	'#define TICK2USEC(value) ((uint64_t)(value))' \
	'clock_t clock_systimer(void);' >"$tmp/stubs/tinyara/clock.h"
printf '%s\n' '#define noreturn_function __attribute__((noreturn))' \
	>"$tmp/stubs/tinyara/compiler.h"
for source in mem_leak_checker_pause.c mem_leak_checker_pause_terminal.c \
	mem_leak_checker_pause_service.c mem_leak_checker_pause_service_stub.c \
	mem_leak_checker_pause_owner.c; do
	"$cc" -std=c11 -Wall -Wextra -Werror -I"$tmp/stubs" \
		-I"$root/os/kernel/debug" -c \
		"$root/os/kernel/debug/$source" -o "$tmp/$source.o"
done
cp "$tmp/owner-config.h" "$tmp/stubs/tinyara/config.h"
"$cc" -std=c11 -Wall -Wextra -Werror -I"$tmp/stubs" \
	-I"$root/os/kernel/debug" \
	"$root/os/kernel/debug/mem_leak_checker_pause_owner.c" \
	"$root/os/kernel/debug/mem_leak_checker_budget.c" \
	"$root/tools/tests/mem_leak_checker_pause_owner_model.c" \
	-o "$tmp/owner-model"
"$tmp/owner-model" >/dev/null
printf '%s\n' '#define CONFIG_SMP 1' '#define CONFIG_SMP_NCPUS 2' \
	'#define CONFIG_MEM_LEAK_REMOTE_PAUSED_MAX_POLLS 65536' \
	>"$tmp/stubs/tinyara/config.h"
"$cc" -std=c11 -Wall -Wextra -Werror -pthread -I"$tmp/stubs" \
	-I"$root/os/kernel/debug" \
	"$root/os/kernel/debug/mem_leak_checker_pause.c" \
	"$root/os/kernel/debug/mem_leak_checker_pause_terminal.c" \
	"$root/os/kernel/debug/mem_leak_checker_lifecycle.c" \
	"$root/os/kernel/debug/mem_leak_checker_pause_owner.c" \
	"$root/os/kernel/debug/mem_leak_checker_budget.c" \
	"$root/tools/tests/mem_leak_checker_production_model.c" \
	-o "$tmp/production-model"
for iteration in $(seq 1 100); do
	"$tmp/production-model" >/dev/null
done
printf '%s\n' '#undef CONFIG_SMP' '#undef CONFIG_SMP_NCPUS' \
	'#define CONFIG_MEM_LEAK_REMOTE_PAUSED_MAX_POLLS 65536' \
	>"$tmp/stubs/tinyara/config.h"
for source in mem_leak_checker_pause_service.c \
	mem_leak_checker_pause_service_stub.c mem_leak_checker_pause_owner.c; do
	"$cc" -std=c11 -Wall -Wextra -Werror -I"$tmp/stubs" \
		-I"$root/os/kernel/debug" -c \
		"$root/os/kernel/debug/$source" -o "$tmp/up-$source.o"
done
printf '%s\n' '#define CONFIG_SMP 1' '#define CONFIG_SMP_NCPUS 2' '#define CONFIG_MEM_LEAK_REMOTE_PAUSED_MAX_POLLS 65536' >"$tmp/stubs/tinyara/config.h"

mkdir -p "$tmp/irq-stubs/tinyara" "$tmp/irq-stubs/arch" \
	"$tmp/irq-stubs/sched" "$tmp/irq-stubs/irq" "$tmp/irq-stubs/debug"
printf '%s\n' '#include <stddef.h>' '#include <stdint.h>' '#include <stdbool.h>' \
	'#define CONFIG_IRQCOUNT 1' '#define CONFIG_SMP 1' \
	'#define CONFIG_SMP_NCPUS 2' '#define CONFIG_MEM_LEAK_CHECKER 1' \
	'#define FAR' '#define DEBUGASSERT(value) assert(value)' \
	'#define DEBUGVERIFY(value) assert(value)' >"$tmp/irq-stubs/tinyara/config.h"
printf '%s\n' '#define OSINIT_TASKLISTS 1' '#define OSINIT_OSREADY 2' \
	'extern int g_os_initstate;' >"$tmp/irq-stubs/tinyara/init.h"
printf '%s\n' '#include <stdbool.h>' '#include <stdint.h>' \
	'typedef unsigned int spinlock_t;' 'typedef uint32_t cpu_set_t;' \
	'#define SP_UNLOCKED 0' '#define SP_LOCKED 1' \
	'extern volatile spinlock_t g_cpu_irqlock;' \
	'static inline int spin_trylock_wo_note(volatile spinlock_t *value) {' \
	'  return __atomic_exchange_n(value, SP_LOCKED, __ATOMIC_ACQ_REL); }' \
	'static inline void spin_unlock_wo_note(volatile spinlock_t *value) {' \
	'  __atomic_store_n(value, SP_UNLOCKED, __ATOMIC_RELEASE); }' \
	'static inline bool spin_islocked(volatile spinlock_t *value) {' \
	'  return __atomic_load_n(value, __ATOMIC_ACQUIRE) == SP_LOCKED; }' \
	'static inline void spin_setbit(volatile cpu_set_t *set, int cpu,' \
	' volatile spinlock_t *setlock, volatile spinlock_t *irqlock) {' \
	'  (void)setlock; (void)irqlock; *set |= (cpu_set_t)1u << cpu;' \
	' }' \
	'static inline void spin_clrbit(volatile cpu_set_t *set, int cpu,' \
	' volatile spinlock_t *setlock, volatile spinlock_t *irqlock) {' \
	'  (void)setlock; *set &= ~((cpu_set_t)1u << cpu);' \
	'  if (*set == 0) __atomic_store_n(irqlock, SP_UNLOCKED, __ATOMIC_RELEASE);' \
	' }' >"$tmp/irq-stubs/tinyara/spinlock.h"
printf '%s\n' '#include <stdbool.h>' '#include <stdint.h>' \
	'typedef uint32_t irqstate_t;' \
	'irqstate_t irqsave(void);' 'void irqrestore(irqstate_t flags);' \
	'bool up_irq_saved_enabled(irqstate_t flags);' \
	'bool up_interrupt_context(void);' 'int this_cpu(void);' \
	'bool up_cpu_pausereq(int cpu);' 'void up_cpu_paused_save(void);' \
	'bool up_cpu_paused(int cpu);' 'void up_cpu_paused_restore(void);' \
	'void up_release_pending(void);' 'struct tcb_s *this_task(void);' \
	>"$tmp/irq-stubs/arch/irq.h"
printf '%s\n' '#include <stdbool.h>' '#include <stdint.h>' \
	'struct tcb_s { int irqcount; };' \
	'struct readytorun_s { void *head; };' \
	'extern struct readytorun_s g_pendingtasks;' \
	'struct tcb_s *current_task(int cpu);' \
	'static inline bool sched_islocked_global(void) { return false; }' \
	>"$tmp/irq-stubs/sched/sched.h"
: >"$tmp/irq-stubs/tinyara/sched_note.h"
: >"$tmp/irq-stubs/irq/irq.h"
printf '%s\n' '#include <stdbool.h>' '#include <stdint.h>' \
	'bool mlc_pause_poll_pending(int cpu, uintptr_t flags);' \
	'bool mlc_pause_service_poll(int cpu, uintptr_t flags);' \
	>"$tmp/irq-stubs/debug/mem_leak_checker_pause.h"
"$cc" -std=c11 -Wall -Wextra -Werror -I"$tmp/irq-stubs" \
	-c "$root/os/kernel/irq/irq_csection.c" -o "$tmp/irq-csection.o"
"$cc" -std=c11 -Wall -Wextra -Werror -I"$tmp/irq-stubs" \
	"$tmp/irq-csection.o" "$root/tools/tests/mem_leak_checker_irq_csection_model.c" \
	-o "$tmp/irq-model"
"$tmp/irq-model" >/dev/null

python3 -I -B - "$root" <<'PY'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
pause = (root / "os/kernel/debug/mem_leak_checker_pause.c").read_text()
terminal = (root / "os/kernel/debug/mem_leak_checker_pause_terminal.c").read_text()
service = (root / "os/kernel/debug/mem_leak_checker_pause_service.c").read_text()
owner = (root / "os/kernel/debug/mem_leak_checker_pause_owner.c").read_text()
domain = (root / "os/kernel/debug/mem_leak_checker_domain.c").read_text()
header = (root / "os/kernel/debug/mem_leak_checker_pause.h").read_text()
irq = (root / "os/kernel/irq/irq_csection.c").read_text()
checker = (root / "os/kernel/debug/mem_leak_checker.c").read_text()
make = (root / "os/kernel/debug/Make.defs").read_text()
assert "__atomic_compare_exchange_n" in pause
assert "__ATOMIC_RELEASE" in pause and "__ATOMIC_ACQUIRE" in pause
assert "__ATOMIC_ACQ_REL" in pause
assert "SP_DMB()" in pause
assert "remote_wait_remaining" in terminal
assert "mlc_pause_sgi_drain" in service
assert "up_mem_leak_monotonic_usec()" in service
assert "MLC_PAUSE_REQUEST_TARGET_US" in owner
assert "MLC_PAUSE_ACCEPT_TARGET_US" in owner
assert "MLC_PAUSE_CANCEL_TARGET_US" in owner
assert "MLC_PAUSE_WORK_STOP_US" in owner
assert "MLC_PAUSE_RESUME_TARGET_US" in owner
assert "MLC_PAUSE_TERMINAL_LIMIT_US" in owner
assert "mlc_pause_deadline_after" in header
assert "epoch_usec +" in header
assert "epoch_usec +" not in owner
assert "epoch_usec +" not in service
assert "mlc_pause_owner_begin(&guard->pause_owner," in domain
assert "MLC_RESOURCE_PAUSE" in domain
assert "remote_wait_remaining" in header
assert "request_pending" in header and "sgi_outstanding" in header
assert irq.count("mlc_pause_poll_pending(cpu, (uintptr_t)") >= 2
assert "mlc_pause_service_poll(cpu," in irq
assert "irqrestore(ret);" in irq and "goto try_again;" in irq
assert "static void mlc_fatal_stop(enum mlc_fatal_reason_e reason) noreturn_function" in checker
for status in ("MLC_RESET_RESUME_AMBIGUOUS", "MLC_RESET_CANCEL_AMBIGUOUS",
               "MLC_RESET_REMOTE_COUNTER_EXHAUSTED", "MLC_RESET_CLOCK_INVALID",
               "MLC_RESET_MAILBOX_PROTOCOL"):
    assert status in checker
assert "board_reset(mlc_fatal_reset_status(reason))" in checker
assert "up_lowputc" in checker
assert "registers[REG_R4 + index]" in service
assert "registers[REG_SP]" in service and "registers[REG_CPSR]" in service
assert "mlc_pause_abort_unsent" in service
assert "mlc_lifecycle_set_epoch" in domain or "epoch_usec" in domain
assert "lifecycle->epoch_usec" in domain
assert "up_mem_leak_monotonic_usec()" in checker
assert "mem_leak_checker_pause.c" in make
for forbidden in ("printf(", "malloc(", "sem_", "PANIC("):
    fatal = checker[checker.index("static void mlc_fatal_stop(enum mlc_fatal_reason_e reason)\n{"):checker.index("void mlc_pause_fatal_dispatch(")]
    assert forbidden not in fatal

PY

mutation_results="$tmp/mutation-results"
: >"$mutation_results"
run_mutation() {
	name=$1
	source=$2
	mutation_kind=$3
	shift 3
	mutant="$tmp/mutant-$name.c"
	cp "$source" "$mutant"
	"$@" "$mutant"
	case "$name" in
		service*)
			cp "$root/tools/tests/mem_leak_checker_production_model.c" \
				"$tmp/production-mutant-$name.c"
			perl -0pi -e "s/mem_leak_checker_pause_service\\.c/mutant-$name.c/" \
				"$tmp/production-mutant-$name.c"
			"$cc" -std=c11 -Wall -Wextra -Werror -pthread -I"$tmp/stubs" \
				-I"$tmp" -I"$root/os/kernel/debug" \
				"$root/os/kernel/debug/mem_leak_checker_pause.c" \
				"$root/os/kernel/debug/mem_leak_checker_pause_terminal.c" \
				"$root/os/kernel/debug/mem_leak_checker_lifecycle.c" \
				"$root/os/kernel/debug/mem_leak_checker_pause_owner.c" \
				"$root/os/kernel/debug/mem_leak_checker_budget.c" \
				"$tmp/production-mutant-$name.c" -o "$tmp/mutant-$name"
		;;
		irq*)
			"$cc" -std=c11 -Wall -Wextra -Werror -I"$tmp/irq-stubs" \
				"$mutant" "$root/tools/tests/mem_leak_checker_irq_csection_model.c" \
				-o "$tmp/mutant-$name"
		;;
		*)
			case "$name" in
				counter|regression|abort)
					objects=("$root/os/kernel/debug/mem_leak_checker_pause.c")
				;;
				dmb|release_order|publish_cas|claim_cas|cancel_cas|resume_cas)
					objects=("$root/os/kernel/debug/mem_leak_checker_pause_terminal.c")
				;;
				recycle_release|sgi_drain)
					objects=("$root/os/kernel/debug/mem_leak_checker_pause.c")
				;;
				*) return 64 ;;
			esac
			"$cc" -std=c11 -Wall -Wextra -Werror -DMLC_PAUSE_HOST_TEST \
				-I"$root/os/kernel/debug" "$mutant" "${objects[@]}" \
				"$root/os/kernel/debug/mem_leak_checker_budget.c" \
				"$root/tools/tests/mem_leak_checker_pause_model.c" \
				-o "$tmp/mutant-$name"
			;;
	esac
	set +e
	python3 -I -B - "$tmp/mutant-$name" "$mutation_kind" <<'PY'
import subprocess, sys
try:
    result = subprocess.run(sys.argv[1:], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, timeout=5)
except subprocess.TimeoutExpired:
    raise SystemExit(124)
raise SystemExit(result.returncode)
PY
	code=$?
	set -e
	[ "$code" -ne 0 ] || return 1
	printf '%s\t%s\t%s\n' "$name" "$code" \
		"$(shasum -a 256 "$mutant" | awk '{print $1}')" >>"$mutation_results"
}

run_mutation counter "$root/os/kernel/debug/mem_leak_checker_pause_terminal.c" failure \
	perl -0pi -e 's/next = remaining - 1;/next = remaining;/'
run_mutation regression "$root/os/kernel/debug/mem_leak_checker_pause_terminal.c" failure \
	perl -0pi -e 's/now == 0 \|\| now < last/now == 0/'
run_mutation abort "$root/os/kernel/debug/mem_leak_checker_pause_terminal.c" failure \
	perl -0pi -e 's/(expected,\n\s*)MLC_PAUSE_CANCELLED, false/$1MLC_PAUSE_PAUSE_REQ, false/'

run_mutation dmb "$root/os/kernel/debug/mem_leak_checker_pause.c" failure \
	perl -0pi -e 's/SP_DMB\(\);/do { } while (0);/'
run_mutation release_order "$root/os/kernel/debug/mem_leak_checker_pause.c" failure \
	perl -0pi -e 's/__ATOMIC_RELEASE/__ATOMIC_RELAXED/'
run_mutation publish_cas "$root/os/kernel/debug/mem_leak_checker_pause.c" failure \
	perl -0pi -e 's/(!compare_acq_rel\(&mailbox->state, &expected, )MLC_PAUSE_PAUSE_REQ/$1MLC_PAUSE_CLAIMED_IRQ/'
run_mutation claim_cas "$root/os/kernel/debug/mem_leak_checker_pause.c" failure \
	perl -0pi -e 's/paused = path == MLC_PAUSE_SERVICE_IRQ \? MLC_PAUSE_PAUSED_IRQ :\n\s*MLC_PAUSE_PAUSED_POLL;/paused = MLC_PAUSE_PAUSE_REQ;/'
run_mutation cancel_cas "$root/os/kernel/debug/mem_leak_checker_pause.c" failure \
	perl -0pi -e 's/(compare_acq_rel\(&mailbox->state, &expected,\n\s*)MLC_PAUSE_CANCEL_REQ_UNCLAIMED/$1MLC_PAUSE_PAUSE_REQ/'
run_mutation resume_cas "$root/os/kernel/debug/mem_leak_checker_pause.c" happy \
	perl -0pi -e 's/(int mlc_pause_owner_resume.*?if \(compare_acq_rel\(&mailbox->state, &expected, )MLC_PAUSE_RESUME_REQ_IRQ/$1MLC_PAUSE_PAUSED_IRQ/s'
run_mutation recycle_release "$root/os/kernel/debug/mem_leak_checker_pause_terminal.c" failure \
	perl -0pi -e 's/store_release\(&mailbox->token, 0\);/store_release\(&mailbox->token, 1\);/'
run_mutation sgi_drain "$root/os/kernel/debug/mem_leak_checker_pause_terminal.c" failure \
	perl -0pi -e 's/uint32_t expected = 1;/uint32_t expected = 0;/'

run_mutation service_irq_path "$root/os/kernel/debug/mem_leak_checker_pause_service.c" failure \
	perl -0pi -e 's/if \(path == MLC_PAUSE_SERVICE_IRQ\) \{\n\t\tif \(!decode_irq_context/if (path == MLC_PAUSE_SERVICE_POLL) {\n\t\tif (!decode_irq_context/'
run_mutation service_poll_restore "$root/os/kernel/debug/mem_leak_checker_pause_service.c" failure \
	perl -0pi -e 's/path == MLC_PAUSE_SERVICE_POLL && !initial_irqs_enabled/path == MLC_PAUSE_SERVICE_POLL \&\& initial_irqs_enabled/'
run_mutation service_drain_check "$root/os/kernel/debug/mem_leak_checker_pause_service.c" failure \
	perl -0pi -e 's/if \(mlc_pause_sgi_drain\(mailbox, token\) < 0\) \{/if (false) {/'

run_mutation irq_restore "$root/os/kernel/irq/irq_csection.c" failure \
	perl -0pi -e 's/irqrestore\(ret\);\n\s*goto try_again;/irqrestore((irqstate_t)0);\n\t\t\t\tgoto try_again;/'
run_mutation irq_retry "$root/os/kernel/irq/irq_csection.c" failure \
	perl -0pi -e 's/(irqrestore\(ret\);\n\s*)goto try_again;/$1if (ret == 0) goto try_again;\n\t\t\t\treturn ret;/'

run_mutation service "$root/os/kernel/debug/mem_leak_checker_pause_service.c" happy \
	perl -0pi -e 's/registers\[REG_SP\]/registers[REG_CPSR]/g'

if [ -n "${MLC_TASK9_EVIDENCE_OUT:-}" ]; then
	python3 -I -B - "$root" "$tmp" "$cc" "$MLC_TASK9_EVIDENCE_OUT" <<'PY'
import hashlib, json, pathlib, shutil, subprocess, sys
root, temporary, compiler, output = map(pathlib.Path, sys.argv[1:])
resolved = pathlib.Path(shutil.which(str(compiler)) or compiler).resolve()
sources = [
    "os/kernel/debug/mem_leak_checker_pause.c",
    "os/kernel/debug/mem_leak_checker_pause_terminal.c",
    "os/kernel/debug/mem_leak_checker_pause_service.c",
    "os/kernel/debug/mem_leak_checker_pause_service_stub.c",
    "os/kernel/debug/mem_leak_checker_pause.h",
    "os/kernel/debug/mem_leak_checker_pause_owner.c",
    "os/kernel/debug/mem_leak_checker_lifecycle.c",
	"os/kernel/debug/mem_leak_checker_budget.c",
    "os/kernel/debug/mem_leak_checker_lifecycle.h",
    "os/kernel/debug/mem_leak_checker.c",
    "os/kernel/debug/mem_leak_checker_domain.c",
    "os/kernel/irq/irq_csection.c",
    "os/arch/arm/src/armv7-a/arm_cpupause.c",
    "os/arch/arm/src/amebasmart/amebasmart_systemreset.c",
    "tools/tests/mem_leak_checker_pause_model.c",
    "tools/tests/mem_leak_checker_pause_owner_model.c",
    "tools/tests/mem_leak_checker_production_model.c",
    "tools/tests/mem_leak_checker_irq_csection_model.c",
]
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
rtl = (root / "os/arch/arm/src/amebasmart/amebasmart_systemreset.c").read_text()
if "board_reset" not in rtl or "up_systemreset" not in rtl or "sys_reset();" not in rtl:
    raise SystemExit("RTL reset source audit could not bind board_reset to sys_reset")
objects = sorted(temporary.glob("*.o"))
mutation_rows = []
for line in (temporary / "mutation-results").read_text().splitlines():
    name, exit_code, mutant_sha256 = line.split("\t")
    mutation_rows.append({"name": name, "exit": int(exit_code),
                          "source_mutant_sha256": mutant_sha256})
expected_mutations = {
    "counter", "regression", "abort", "dmb", "release_order",
    "publish_cas", "claim_cas", "cancel_cas", "resume_cas",
    "recycle_release", "sgi_drain", "service_irq_path",
    "service_poll_restore", "service_drain_check", "irq_restore",
    "irq_retry", "service",
}
if {row["name"] for row in mutation_rows} != expected_mutations:
    raise SystemExit("mutation audit did not execute the expected source mutants")
if any(row["exit"] == 0 for row in mutation_rows):
    raise SystemExit("mutation audit accepted a surviving source mutant")
payload = {
    "compiler": str(resolved),
    "compiler_sha256": digest(resolved),
    "compiler_version": subprocess.check_output([resolved, "--version"], text=True).splitlines()[0],
    "model_sha256": digest(temporary / "model"),
    "owner_model_sha256": digest(temporary / "owner-model"),
    "production_model_sha256": digest(temporary / "production-model"),
    "object_sha256": {path.name: digest(path) for path in objects},
    "source_sha256": {name: digest(root / name) for name in sources},
    "static_audit": "source_presence_audit",
    "mutation_audit": {"status": "executed_source_mutants",
                        "pass": True, "count": len(mutation_rows),
                        "expected": sorted(expected_mutations),
                        "cases": mutation_rows},
    "executable_route_audit": {
        "unclaimed_cancel": True, "claimed_cancel": True,
        "late_resume": True, "irq_restore": True,
        "irq_retry": True, "ordinary_critical_reacquire": True,
        "delayed_irq_across_recycle": True,
    },
    "arm_sgi_generation": "deferred_no_payload_metadata",
    "production_repeat_count": 100,
    "up_validation": "compile_only",
    "target_object_binding": "deferred_no_target_toolchain",
    "rtl_closure": "deferred_unsupported_vendor_sys_reset",
    "rtl_source_sha256": digest(root / "os/arch/arm/src/amebasmart/amebasmart_systemreset.c"),
    "rtl_unsupported_reason": "vendor sys_reset/APSYS System_Reset implementation is outside this repository",
    "compile_commands": [
        "cc -std=c11 -Wall -Wextra -Werror -DMLC_PAUSE_HOST_TEST pause.c pause_terminal.c pause_model.c",
        "cc -std=c11 -Wall -Wextra -Werror -pthread pause.c pause_terminal.c lifecycle.c pause_owner.c production_model.c",
        "cc -std=c11 -Wall -Wextra -Werror -I stubs -c pause_service.c pause_service_stub.c pause_owner.c",
        "cc -std=c11 -Wall -Wextra -Werror -I irq-stubs -c irq_csection.c irq_csection_model.c",
    ],
}
output.write_text(json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n")
PY
fi

case "$kind" in
happy)
	printf 'MLC_TASK9_ROUTE kind=happy fixtures=%s post_commit=true\n' "$fixtures"
	printf '%s\n' 'MLC_TASK9_MODEL status=PASS smp2=executed owner_thresholds=10,20,40,78,80,95 clock=regressed,frozen paths=irq,poll up=compile_only'
	printf '%s\n' 'MLC_TASK9_PRODUCTION status=PASS validation=host_compile_and_executable_model mutation=executable_source_mutants lifecycle=request,resume,recycle frame=structured fatal=deferred rtl=unsupported target_objects=deferred'
	;;
failure)
	printf 'MLC_TASK9_ROUTE kind=failure fixtures=%s post_commit=true\n' "$fixtures"
	printf '%s\n' 'MLC_TASK9_FAILURE status=PASS model=executed cancel=unclaimed,claimed irq_restore=executed_model try_again=executed_model ordinary_critical=executed_model delayed_sgi=drained duplicate_sgi=fatal initially_masked=rejected stale=host_token_bound arm_sgi_generation=deferred_no_payload token_exhaustion=fail_closed counter=max,max_plus_one'
	;;
fatal)
	printf 'MLC_TASK9_ROUTE kind=fatal fixtures=%s post_commit=true\n' "$fixtures"
	printf '%s\n' 'MLC_TASK9_FATAL status=PASS model=executed markers=REMOTE_COUNTER_EXHAUSTED,CLOCK_INVALID,MAILBOX_PROTOCOL rtl_closure=deferred_unsupported_vendor_sys_reset target_objects=deferred fixed_reset_statuses=5 hardware_validation=skipped_by_user reset_executed=false'
	;;
*) exit 64 ;;
esac
printf '%s\n' 'MLC_TASK9_QEMU status=deferred_unexecuted_baseline_link_failure'
