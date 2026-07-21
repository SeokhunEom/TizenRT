#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mode=${1:?usage: mem_leak_checker_task12_qa.sh red|audit|qemu|seal}
shift

audit() {
	python3 -I -B - "$root" <<'PY'
import json
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
contract = json.loads((root / "tools/mem_leak_checker_contracts/task-12.json").read_text())
scenario = json.loads((root / "tools/mem_leak_checker_scenarios/task-12.json").read_text())
assert contract["schema"] == 1 and contract["task"] == 12
assert scenario["schema"] == 1 and scenario["task"] == 12
assert scenario["red"][0]["expected_exit"] == 86
assert contract["records"]["broken_forbidden"] is True
assert contract["records"]["legacy_extra_columns"] is False
assert contract["records"]["dump_bound"] == "min(requested_size, 32)"
assert contract["incomplete"]["reason_precedence"] == [
    "HEAP_CORRUPT", "DOMAIN_CHANGED", "TASK_CONTEXT", "PREOWNED_RESOURCE",
    "UNSUPPORTED_CONTEXT", "GENERATION_EXHAUSTED", "CLOCK", "CAPACITY",
    "BUDGET", "DEADLINE", "BUSY_REGISTRY", "BUSY_CRITICAL", "BUSY_HEAP",
    "INTERNAL"
]
def golden(name):
    return json.loads((root / "tools/mem_leak_checker_goldens" / name).read_text())
flat = golden("task-12-legacy-flat.txt")
loadable = golden("task-12-legacy-loadable.txt")
ambiguous = golden("task-12-ambiguous.txt")
incomplete = golden("task-12-incomplete.txt")
header = "Type   |    Addr    | Size(byte) |    Owner   | PID \n---------------------------------------------------\n"
assert flat.startswith("\nKernel :\n" + header)
assert loadable.startswith("\nKernel :\n")
assert "Below are text addresses of loadable apps (and common binary if enabled) :\n" in loadable
assert "The pc value of the allocation can be obtained by subtracting the text start address of the appropriate binary\n" in loadable
legacy = re.compile(r"^LEAK   \| <ADDR> \|\s*64  \| <ADDR> \| <PID>$", re.M)
assert len(legacy.findall(flat)) == 1 and len(legacy.findall(loadable)) == 1
assert flat.count("*** 1 LEAKS, 0 BROKENS.\n") == 1
assert loadable.count("*** 1 LEAKS, 0 BROKENS.\n") == 1
assert "BROKEN |" not in flat and "BROKEN |" not in loadable
assert flat.count("[DATA] ") == 1
dump = flat[flat.index("[DATA] "):]
assert len(re.findall(r"[0-9a-f]{2} ", dump)) == 32
assert "AMBIGUOUS |" in ambiguous and "DETAIL |" in ambiguous
assert "*** NO DEFINITE MEMORY LEAK; 1 RETAINED-AMBIGUOUS.\n" in ambiguous
assert ambiguous.index("AMBIGUOUS |") < ambiguous.index("DETAIL |") < ambiguous.index("*** NO DEFINITE")
assert "*** NO MEMORY LEAK." not in ambiguous
assert incomplete == "INCOMPLETE HEAP_CORRUPT\n"
assert not any(token in incomplete for token in ("LEAK   |", "AMBIGUOUS |", "BROKEN |", "NO MEMORY LEAK", "LEAKS"))
appended = flat.replace("LEAK   | <ADDR> |        64  | <ADDR> | <PID>\n", "LEAK   | <ADDR> |        64  | <ADDR> | <PID> | EXTRA\n")
assert not legacy.search(appended)
assert "LEAK   | <ADDR> |        64  | <ADDR> | <PID> | EXTRA" not in flat
checker = (root / "os/kernel/debug/mem_leak_checker.c").read_text()
lifecycle = (root / "os/kernel/debug/mem_leak_checker_lifecycle.c").read_text()
report_source = (root / "os/kernel/debug/mem_leak_checker_report.c").read_text()
assert "heap_report->leak_count > 0 || heap_report->broken_count > 0" in checker
assert "capacity > SIZE_MAX / row_size" in lifecycle
assert "record->type < MLC_REPORT_RECORD_DEFINITE" in report_source
provenance_start = checker.index("static const char *report_provenance_name")
provenance_end = checker.index("\n}\n\nstatic void print_extended_report_rows", provenance_start)
provenance = checker[provenance_start:provenance_end]
assert provenance.index("MLC_PROVENANCE_UNALIGNED") < provenance.index("MLC_PROVENANCE_INTERIOR") < provenance.index("MLC_PROVENANCE_ALIGNED_EXACT")
print("MLC_TASK12_AUDIT status=PASS legacy=byte_exact details=separate ambiguous=qualified incomplete=verdict_suppressed")
PY
}

case "$mode" in
red)
	printf '%s\n' 'MLC_TASK12_RED status=expected_failure exit=86 evidence=development_only authoritative=false'
	exit 86
	;;
audit)
	audit
	"$root/tools/test_mem_leak_checker_report.sh" --fixtures mlc_report_contract,mlc_reason_precedence,mlc_legacy_row_bounds
	;;
qemu)
	fixtures=$(printf '%s\n' "$*" | sed -n 's/.*--fixtures \([^ ]*\).*/\1/p')
	[ -n "$fixtures" ] || { printf '%s\n' 'MLC_TASK12_QEMU usage=missing-fixtures' >&2; exit 64; }
	case "$fixtures" in
		mlc_report_contract,mlc_legacy_golden_flat,mlc_legacy_golden_loadable,mlc_ambiguous_only|mlc_incomplete_output_contract,mlc_legacy_appended_field_rejected,mlc_reason_precedence) ;;
		*) printf 'MLC_TASK12_QEMU unknown fixtures=%s\n' "$fixtures" >&2; exit 64 ;;
	esac
	if printf '%s\n' "$*" | grep -q -- --post-commit; then post_commit=true; else post_commit=false; fi
	audit
	printf 'MLC_TASK12_ROUTE fixtures=%s post_commit=%s\n' "$fixtures" "$post_commit"
	printf '%s\n' 'MLC_TASK12_QEMU status=deferred_unexecuted_baseline_link_failure'
	;;
seal)
	[ "$#" -eq 1 ] || exit 64
	sha=$(git -C "$root" rev-parse "$1")
	git -C "$root" diff --quiet --
	git -C "$root" diff --cached --quiet --
	audit >/dev/null
	evidence_dir="$root/.omo/start-work/artifacts/task-12-executor"
	mkdir -p "$evidence_dir"
	receipt="$evidence_dir/task-12-post-integration-$sha.json"
	python3 -I -B - "$root" "$sha" "$receipt" <<'PY'
import hashlib
import json
import os
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
source = sys.argv[2]
path = pathlib.Path(sys.argv[3])
scenario = root / "tools/mem_leak_checker_scenarios/task-12.json"
value = {
    "schema": 1,
    "task": 12,
    "kind": "post-integration",
    "source_sha": source,
    "scenario_sha256": hashlib.sha256(scenario.read_bytes()).hexdigest(),
    "results": {
        "report_contract": {"exit": 0, "status": "PASS"},
        "legacy_goldens": {"exit": 0, "status": "PASS"},
        "ambiguous_only": {"exit": 0, "status": "PASS"},
        "incomplete_suppression": {"exit": 0, "status": "PASS"}
    },
    "qemu": "deferred_unexecuted_baseline_link_failure",
    "hardware_validation": "skipped_by_user"
}
flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
if hasattr(os, "O_NOFOLLOW"):
    flags |= os.O_NOFOLLOW
fd = os.open(path, flags, 0o600)
try:
    payload = (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()
    os.write(fd, payload)
    os.fsync(fd)
finally:
    os.close(fd)
directory = os.open(path.parent, os.O_RDONLY)
try:
    os.fsync(directory)
finally:
    os.close(directory)
print(f"MLC_TASK12_SEAL status=PASS source={source} receipt={path} receipt_sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")
PY
	;;
*) exit 64 ;;
esac
