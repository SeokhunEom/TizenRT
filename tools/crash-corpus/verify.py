#!/usr/bin/env python3
"""Verify corpus integrity and expected observations without changing evidence."""
import hashlib
import json
from pathlib import Path
import re
import sys


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(root):
    provenance = json.loads((root / "provenance.json").read_text())
    summary = json.loads((root / "summary.json").read_text())
    assert provenance["evidence_kind"] == "actual-qemu-runtime"
    assert provenance["synthetic_log"] is False
    assert provenance["hardware_validated"] is False
    assert re.fullmatch(r"[0-9a-f]{64}", provenance["elf_sha256"])
    assert sha256(root / "kernel.config") == provenance["config_sha256"]
    if provenance.get("symbols_sha256"):
        assert sha256(root / "System.map") == provenance["symbols_sha256"]
    for path, digest in provenance["source_sha256"].items():
        assert sha256(root / "source" / path) == digest, path
    assert len(summary["results"]) == summary["cases"]
    observed = 0
    for result in summary["results"]:
        case = result["case"]
        assert json.loads((root / f"{case}.json").read_text()) == result, case
        log = root / result["log"]
        assert sha256(log) == result["log_sha256"], case
        assert log.stat().st_size == result["log_bytes"], case
        text = log.read_text(errors="replace")
        if result["expected_observed"]:
            assert re.search(result["expected_pattern"], text, re.I), case
            observed += 1
        if result["guest_prompt_after_fault"]:
            assert re.search(r"(?:Assertion failed at file:|PANIC!!!).*TASH>>", text, re.S), case
        assert result["observation_timed_out"] == (result["termination_reason"] == "timeout"), case
        assert result["hardware_validated"] is False, case
    assert observed == summary["observed"]
    print(f"Verified {len(summary['results'])} raw logs; {observed} expected observations; ELF {provenance['elf_sha256']}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify.py EVIDENCE_DIRECTORY")
    verify(Path(sys.argv[1]))
