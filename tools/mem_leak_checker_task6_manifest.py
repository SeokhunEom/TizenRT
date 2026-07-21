from typing import Final

TASK: Final = 6
SCHEMA_VERSION: Final = 2
WORK_ID: Final = "mem-leak-checker-hardening-257754dc"
PLAN_PATH: Final = ".omo/plans/mem-leak-checker-hardening.md"
SESSION_ID: Final = "codex:257754dc-1fe6-461f-86d4-57a7d2b9fb8d"
BASELINE_SHA: Final = "c93078ab05bb6463467669fb6ee19bb75ee7eaba"
EVIDENCE_PATH: Final = ".omo/start-work/artifacts/task-6-executor"
NORMALIZED_PLAN_SHA256: Final = (
    "47d8f48a15eaae0f390737bcd3e488948022e3780dcedcf77445de13003f1c93"
)
RED_FIXTURE: Final = "mlc_domain_pin_production_path"
CASE_FIXTURES: Final = {
    "happy": (
        "mlc_domain_pin_production_path", "mlc_try_heap_fresh_accounting",
        "mlc_heap_release_nested_critical",
    ),
    "failure": (
        "mlc_domain_unload_churn", "mlc_remote_critical_then_heap",
        "mlc_bounded_acquire_busy", "mlc_heap_preowned",
        "mlc_heap_accounting_fault", "mlc_heap_release_irqwaitlock_forbidden",
    ),
    "fatal": ("mlc_heap_release_ownership_fatal", "mlc_domain_unpin_fatal"),
}
THREAT_MODEL: Final = {
    "active_same_uid_race": "excluded",
    "checked_inputs": "postvalidated",
    "trusted_initial_root": "trusted_task6_shell_entrypoint_cannot_self_authenticate",
    "shell_startup_before_script_body": "excluded",
    "scope": "cooperative_local_qa",
    "trusted_entrypoint_bootstrap": "required_before_validation",
}

SOURCE_PATHS: Final = (
    "os/binfmt/binfmt_execmodule.c",
    "os/binfmt/binfmt_exit.c",
    "os/binfmt/binfmt_loadbinary.c",
    "os/include/tinyara/binfmt/binfmt.h",
    "os/include/tinyara/mm/mm.h",
    "os/arch/arm/src/common/up_restoretask.c",
    "os/arch/arm/src/armv7-a/arm_syscall.c",
    "os/arch/arm/src/armv7-m/up_svcall.c",
    "os/arch/arm/src/armv8-m/up_svcall.c",
    "os/kernel/binary_manager/binary_manager_load.c",
    "os/kernel/debug/Make.defs",
    "os/kernel/debug/mem_leak_checker.c",
    "os/kernel/debug/mem_leak_checker_domain.c",
    "os/kernel/debug/mem_leak_checker_domain.h",
    "os/kernel/debug/mem_leak_checker_lifecycle.c",
    "os/kernel/debug/mem_leak_checker_pause.h",
    "os/kernel/debug/mem_leak_checker_pause_owner.c",
    "os/kernel/debug/mem_leak_checker_pause_owner.h",
    "os/kernel/irq/irq_csection.c",
    "os/mm/mm_heap/Make.defs",
    "os/mm/mm_heap/mm_getheap.c",
    "os/mm/mm_heap/mm_loadable_domain.c",
    "os/mm/mm_heap/mm_loadable_domain_internal.h",
    "os/mm/mm_heap/mm_loadable_domain_pin.c",
    "os/mm/mm_heap/mm_sem.c",
    "tools/mem_leak_checker_qa.sh",
    "tools/mem_leak_checker_scenarios/task-6.json",
    "tools/mem_leak_checker_task6_domain_test.c",
    "tools/mem_leak_checker_task6_fatal_stubs/tinyara/spinlock.h",
    "tools/mem_leak_checker_task6_fatal_test.c",
    "tools/mem_leak_checker_task6_guard_test.c",
    "tools/mem_leak_checker_task6_host_qa.sh",
    "tools/mem_leak_checker_task6_context.py",
    "tools/mem_leak_checker_task6_files.py",
    "tools/mem_leak_checker_task6_git_trust.py",
    "tools/mem_leak_checker_task6_manifest.py",
    "tools/mem_leak_checker_task6_native_qa.sh",
    "tools/mem_leak_checker_task6_native_sem_test.c",
    "tools/mem_leak_checker_task6_native_support.c",
    "tools/mem_leak_checker_task6_native_support.h",
    "tools/mem_leak_checker_task6_output.py",
    "tools/mem_leak_checker_task6_publish_test.c",
    "tools/mem_leak_checker_task6_production_qa.sh",
    "tools/mem_leak_checker_task6_production_test.c",
    "tools/mem_leak_checker_task6_publish.py",
    "tools/mem_leak_checker_task6_qa.sh",
    "tools/mem_leak_checker_task6_receipt.py",
    "tools/mem_leak_checker_task6_route_tests.py",
    "tools/mem_leak_checker_task6_runner.py",
    "tools/mem_leak_checker_task6_seal.sh",
    "tools/mem_leak_checker_task6_sem_test.c",
    "tools/mem_leak_checker_task6_schema.py",
    "tools/mem_leak_checker_task6_security_tests.py",
    "tools/mem_leak_checker_task6_stubs/assert.h",
    "tools/mem_leak_checker_task6_stubs/debug.h",
    "tools/mem_leak_checker_task6_stubs/semaphore.h",
    "tools/mem_leak_checker_task6_stubs/tinyara/arch.h",
    "tools/mem_leak_checker_task6_stubs/tinyara/clock.h",
    "tools/mem_leak_checker_task6_stubs/tinyara/config.h",
    "tools/mem_leak_checker_task6_stubs/tinyara/irq.h",
    "tools/mem_leak_checker_task6_stubs/tinyara/spinlock.h",
    "tools/mem_leak_checker_task6_stubs/tinyara/mm/mm.h",
    "tools/mem_leak_checker_task6_validate.py",
    "tools/mem_leak_checker_task6_validate_test_support.py",
    "tools/mem_leak_checker_task6_types.py",
    "tools/test_mem_leak_checker_task6_validate.py",
    "tools/mem_leak_checker_task5_qa.sh",
    "tools/mem_leak_checker_task5_stubs/tinyara/clock.h",
) + tuple(
    f"tools/mem_leak_checker_task6_native_stubs/{relative}"
    for relative in (
        "arch/chip/memory_region.h", "arch/irq.h", "assert.h", "debug.h",
        "irq/irq.h", "queue.h",
        "sched.h", "sched/sched.h", "semaphore.h", "tinyara/arch.h",
        "tinyara/compiler.h", "tinyara/config.h", "tinyara/debug/sysdbg.h",
        "tinyara/init.h", "tinyara/irq.h", "tinyara/mm/mm.h",
        "tinyara/sched.h", "tinyara/sched_note.h", "tinyara/spinlock.h",
    )
)
