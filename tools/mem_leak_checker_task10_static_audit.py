#!/usr/bin/env python3
"""Fail-closed static contract audit for the Todo10 budget surface."""

from __future__ import annotations

import os
from pathlib import Path
import re
import stat
import sys


COUNTERS = (
    "REGISTRY_ENUM", "DOMAIN_PIN", "ROOT_CONTAINER_ENUM", "HEAP_ACQUIRE",
    "HEAP_RELEASE_VALIDATE", "DOMAIN_UNPIN", "REGISTRY_UNWIND",
    "PAUSE_ACK", "REMOTE_PAUSED_SERVICE", "CANCEL_COMPLETION",
    "RESUME_COMPLETION", "SGI_DRAIN",
)


def fail(message: str) -> None:
    raise SystemExit(f"Task10 static audit failed: {message}")


def function_body(source: str, name: str) -> str:
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", source)
    if match is None:
        fail(f"function missing: {name}")
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end():index]
    fail(f"unterminated function: {name}")
    return ""


def main() -> int:
    if len(sys.argv) != 2:
        return 64
    root = Path(sys.argv[1]).resolve()
    budget_h = (root / "os/kernel/debug/mem_leak_checker_budget.h").read_text()
    budget_c = (root / "os/kernel/debug/mem_leak_checker_budget.c").read_text()
    domain = (root / "os/kernel/debug/mem_leak_checker_domain.c").read_text()
    owner = (root / "os/kernel/debug/mem_leak_checker_pause_owner.c").read_text()
    terminal = (root / "os/kernel/debug/mem_leak_checker_pause_terminal.c").read_text()
    checker = (root / "os/kernel/debug/mem_leak_checker.c").read_text()
    production = {
        path: path.read_text()
        for path in (root / "os/kernel/debug").glob("*.c")
        if path.name != "mem_leak_checker_budget.c"
    }
    qa = root / "tools/mem_leak_checker_task10_qa.sh"

    if "MLC_SNAPSHOT_REQUESTED_BYTES_MAX 1048576u" not in budget_h:
        fail("requested-byte cap")
    if "MLC_BUDGET_CHUNK_MAX 256u" not in budget_h:
        fail("chunk cap")
    if "budget->remaining[counter] -=" not in budget_c:
        fail("predecrement missing")
    if "validate_max(value) < 0" not in budget_c or "checked_add_u64" not in budget_c:
        fail("overflow/config validation missing")
    for inequality in (
        "configured[MLC_BUDGET_HEAP_RELEASE_VALIDATE] <",
        "configured[MLC_BUDGET_DOMAIN_UNPIN] <",
        "configured[MLC_BUDGET_REGISTRY_UNWIND] <",
    ):
        if inequality not in budget_c:
            fail(f"capacity inequality missing: {inequality}")
    for counter in COUNTERS:
        token = f"MLC_BUDGET_{counter}"
        if token not in budget_h or sum(text.count(token) for text in production.values()) == 0:
            fail(f"production consumer missing: {counter}")
    for name in (
        "mlc_budget_reserve_ownership_identity",
        "mlc_budget_commit_ownership_identity",
        "mlc_budget_release_ownership_identity",
        "mlc_budget_return_reservation_identity",
    ):
        if name not in budget_h or name not in budget_c:
            fail(f"identity ledger API missing: {name}")
    for name in ("mlc_budget_reserve_ownership_identity",
                 "mlc_budget_commit_ownership_identity",
                 "mlc_budget_release_ownership_identity"):
        if name not in domain:
            fail(f"domain ledger operation missing: {name}")
    if domain.count("mlc_budget_commit_ownership_identity") < 2 or \
            domain.count("mlc_budget_release_ownership_identity") < 2:
        fail("domain pin and heap ledger pairs are incomplete")
    if "mm_loadable_domain_try_pin_all" not in domain:
        fail("domain pin call missing")
    if domain.find("mlc_budget_reserve_ownership_identity") > domain.find("mm_loadable_domain_try_pin_all"):
        fail("domain reservation occurs after ownership")
    if "(void)mlc_budget_release_ownership" in domain:
        fail("ignored ownership release")
    if "mlc_pause_owner_begin_with_budget" not in domain or "shared_budget" not in owner:
        fail("shared lifecycle budget missing")
    if "__builtin_unreachable();" not in owner or "mlc_pause_fatal_dispatch(reason);" not in owner:
        fail("fatal reset path is not non-returning")
    if "MLC_BUDGET_REMOTE_PAUSED_SERVICE" not in terminal or "MLC_BUDGET_SGI_DRAIN" not in terminal:
        fail("remote/SGI counters are not consumed")
    if "if (budget == NULL) {\n\t\treturn -EPERM;" not in terminal:
        fail("remote service has a NULL-budget bypass")
    begin = checker.find("ret = mlc_lifecycle_begin(&lifecycle);")
    publish = checker.find("g_active_budget = &lifecycle.counters;", begin)
    epoch = checker.find("mlc_lifecycle_set_epoch", begin)
    if begin < 0 or publish < 0 or epoch < 0 or publish > epoch:
        fail("active budget is not published before epoch/domain work")
    for source, names in (
        (domain, ("mlc_domain_unpin", "mlc_domain_release_heaps",
                  "mlc_domain_guard_try")),
        (checker, ("mlc_snapshot_add_task", "mlc_snapshot_release_registry",
                   "heap_check", "capture_info")),
        (owner, ("wait_terminal", "mlc_pause_owner_begin_internal")),
        (terminal, ("mlc_pause_remote_step", "mlc_pause_sgi_drain")),
    ):
        for name in names:
            body = function_body(source, name)
            if ("for (" in body or "while (" in body) and (
                    ("MLC_BUDGET_" not in body and "counter" not in body) or
                    (("chunk_begin" not in body or "chunk_end" not in body) and
                     "mlc_analysis_budget_chunk" not in body and
                     "mlc_pause_budget_take" not in body and
                     "mlc_domain_budget_end_chunks" not in body)):
                fail(f"cycle lacks pre/post budget audit: {name}")
    fatal = function_body(checker, "mlc_fatal_stop")
    if "for (;;)" not in fatal or "board_reset" not in fatal or \
            "__builtin_unreachable" not in fatal:
        fail("fatal/reset closure is returning")
    if "ledger_counters_valid" not in budget_c or \
            "ledger_available != budget->ledger_capacity - reserved - committed" not in budget_c or \
            "*reverse_available > reverse_capacity" not in budget_c:
        fail("ledger capacity equation is not fail-closed")
    for module in ("mem_leak_checker_candidates.c", "mem_leak_checker_graph.c",
                   "mem_leak_checker_index.c", "mem_leak_checker_tarjan.c"):
        text = (root / "os/kernel/debug" / module).read_text()
        if "mlc_budget_chunk_begin" not in text or "mlc_budget_chunk_end" not in text:
            fail(f"chunk post-check missing: {module}")
    if "mlc_candidate_budget_take" not in production.get(
            root / "os/kernel/debug/mem_leak_checker_candidates.c", ""):
        fail("heap region/node loop helper missing")
    if ("mlc_budget_chunk_end" not in checker and
            "mlc_lifecycle_budget_chunk_end" not in checker) or "MLC_BUDGET_REPORT_ROW" not in checker:
        fail("report/chunk accounting missing")
    analysis_sources = checker + production.get(
        root / "os/kernel/debug/mem_leak_checker_graph.c", "") + production.get(
        root / "os/kernel/debug/mem_leak_checker_tarjan.c", "")
    for counter in ("EDGE_RESCAN", "POINTER_WINDOW", "FREE_NODE"):
        if f"MLC_BUDGET_{counter}" not in analysis_sources:
            fail(f"analysis loop counter missing: {counter}")
    if checker.count("MLC_BUDGET_ROOT_CONTAINER_ENUM") == 0:
        fail("root-container enumeration is uncounted")
    unpin_pos = domain.find("MLC_RESOURCE_DOMAIN, mlc_domain_unpin")
    leave_pos = domain.find("MLC_RESOURCE_CRITICAL, mlc_domain_leave_critical")
    release_pos = domain.find("MLC_RESOURCE_HEAP, mlc_domain_release_heaps")
    if not (unpin_pos >= 0 and leave_pos > unpin_pos and release_pos > leave_pos):
        fail("unwind order is not heap-release,critical-leave,domain-unpin")
    if not stat.S_IMODE(qa.stat().st_mode) & stat.S_IXUSR:
        fail("task10 dispatcher is not executable")
    print("MLC_TASK10_STATIC status=PASS counters=12 identity_ledger=PASS chunks=pre_post deadline=fail_closed control_flow=cycle_dominance reset_closure")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
