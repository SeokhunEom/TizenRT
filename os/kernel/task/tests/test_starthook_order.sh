#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../../../.." && pwd)
task_create="$root/os/kernel/task/task_create.c"
task_header="$root/os/kernel/task/task.h"
provider="$root/os/drivers/os_api_test/kernel/test_task.c"
registry="$root/os/drivers/os_api_test/os_api_test_kernel_registry.inc"

fail()
{
	echo "starthook_order: $1" >&2
	exit 1
}

if awk '
	/static int test_task_create_kernel_with_starthook/ { in_clone = 1 }
	in_clone { body = body $0 "\n" }
	in_clone && /^}/ { exit }
	END {
		if (!in_clone) {
			exit 0
		}
		missing = ""
		if (body !~ /g_alive_taskcount/) {
			missing = "CONFIG_MAX_TASKS guard"
		}
		if (body !~ /heapinfo_check_group_list/) {
			missing = missing (missing == "" ? "" : " and ") "heapinfo group accounting"
		}
		if (missing != "") {
			print "starthook_order: cloned factory bypasses " missing > "/dev/stderr"
			exit 1
		}
		exit 1
	}
' "$provider"; then
	:
else
	exit 1
fi

awk '/^int task_create_with_starthook\(/ { found = 1 } END { exit !found }' "$task_create" ||
	fail "private task_create_with_starthook implementation is missing"
awk '/^int task_create_with_starthook\(/ { found = 1 } END { exit !found }' "$task_header" ||
	fail "private task_create_with_starthook declaration is missing"

hook_line=$(awk '/^[[:space:]]*task_starthook\(tcb,/ { print NR; exit }' "$task_create")
activate_line=$(awk '/^[[:space:]]*\(void\)task_activate\(\(FAR struct tcb_s \*\)tcb\);/ { print NR; exit }' "$task_create")
[ -n "$hook_line" ] || fail "creation core does not install the hook"
[ -n "$activate_line" ] || fail "creation core activation call is missing"
[ "$hook_line" -lt "$activate_line" ] || fail "hook must be installed immediately before task_activate"
[ "$((activate_line - hook_line))" -le 12 ] || fail "unrelated work separates hook installation from activation"

awk '/return thread_create\(name, TCB_FLAG_TTYPE_TASK,/ && /NULL, NULL\);/ { task = 1 }
	/return thread_create\(name, TCB_FLAG_TTYPE_KERNEL,/ && /NULL, NULL\);/ { kernel = 1 }
	END { exit !(task && kernel) }' "$task_create" ||
	fail "task_create and kernel_thread must explicitly pass no hook"

awk '
	/sched_getparam\(0, &caller_param\)/ { getparam = 1 }
	/caller_param\.sched_priority >= SCHED_PRIORITY_MAX/ { bounded = 1 }
	/child_priority = caller_param\.sched_priority \+ 1/ { higher = 1 }
	/sem_post\(&g_test_starthook_started_sem\)/ { started = 1 }
	/sem_wait\(&g_test_starthook_release_sem\)/ { blocked = 1 }
	/task_create_with_starthook\(/ { seam = 1 }
	END { exit !(getparam && bounded && higher && started && blocked && seam) }
' "$provider" || fail "provider does not prove strict higher-priority preemption and semaphore blocking"

if rg -q 'task_create_with_starthook' "$root/os/include"; then
	fail "private creation seam leaked into os/include"
fi

awk '
	/^#if defined\(CONFIG_TC_KERNEL_TASK\) && defined\(CONFIG_SCHED_STARTHOOK\)$/ { predicate = 1; next }
	predicate && /^OS_API_TEST_KERNEL_DESCRIPTOR\(TESTIOC_TASK_STARTHOOK_TEST, 55, test_task, test_task\.c, task_starthook_main, tc_task\.c, CONFIG_TC_KERNEL_TASK\)$/ { row = 1 }
	END { exit !(predicate && row) }
' "$registry" || fail "guarded starthook registry descriptor is incomplete"

echo "starthook_order: PASS"
