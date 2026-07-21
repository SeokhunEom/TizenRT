#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/mlc-task6-native.XXXXXX")
trap 'rm -rf -- "$temp_dir"' EXIT HUP INT TERM

stub="$repo_root/tools/mem_leak_checker_task6_native_stubs"
common_flags=(
	-std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter
	-DCONFIG_ARCH_CORTEXM3
	-I"$stub" -I"$repo_root/os/kernel" -I"$repo_root/tools"
)
common_sources=(
	"$repo_root/os/mm/mm_heap/mm_sem.c"
	"$repo_root/os/kernel/semaphore/sem_trywait.c"
	"$repo_root/os/kernel/semaphore/sem_post.c"
	"$repo_root/os/kernel/semaphore/sem_holder.c"
	"$repo_root/tools/mem_leak_checker_task6_native_support.c"
	"$repo_root/tools/mem_leak_checker_task6_native_sem_test.c"
)

for source in mm_loadable_domain.c mm_loadable_domain_pin.c; do
	cc "${common_flags[@]}" -DMLC_TASK6_USER_PASS=1 \
		-c "$repo_root/os/mm/mm_heap/$source" \
		-o "$temp_dir/user-${source%.c}.o"
done

build_and_run() {
	local variant=$1
	local irq_source=$2
	local symbol
	shift 2
	cc "${common_flags[@]}" "$@" "${common_sources[@]}" \
		"$repo_root/$irq_source" -o "$temp_dir/$variant"
	for symbol in mm_trysemaphore_fresh sem_trywait sem_post \
		sem_addholder sem_releaseholder irq_try_enter_critical_fresh; do
		nm "$temp_dir/$variant" | awk -v wanted="$symbol" \
			'$NF == wanted || $NF == "_" wanted { count++ } \
			 END { exit count == 1 ? 0 : 1 }'
	done
	"$temp_dir/$variant"
}

build_and_run smp_irqcount os/kernel/irq/irq_csection.c \
	-DCONFIG_IRQCOUNT -DCONFIG_SMP -DCONFIG_SMP_NCPUS=2
build_and_run up_irqcount os/kernel/irq/irq_csection.c -DCONFIG_IRQCOUNT
build_and_run up_no_irqcount os/kernel/irq/irq_trycritical.c

if cc "${common_flags[@]}" -DCONFIG_SMP \
	-c "$repo_root/os/kernel/irq/irq_trycritical.c" \
	-o "$temp_dir/unsupported.o" >"$temp_dir/unsupported.log" 2>&1; then
	printf '%s\n' 'SMP without IRQCOUNT unexpectedly compiled' >&2
	exit 1
fi
grep -q 'CONFIG_SMP requires CONFIG_IRQCOUNT' "$temp_dir/unsupported.log"
printf '%s\n' 'MLC_TASK6_NATIVE_VARIANTS status=PASS supported=3 negative=smp_no_irqcount'
