#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import os
import re
import stat
import subprocess
import sys

BOOTSTRAP_PATHS = (
    "tools/mem_leak_checker_qa.sh",
    "tools/mem_leak_checker_task6_qa.sh",
    "tools/mem_leak_checker_task6_seal.sh",
    "tools/mem_leak_checker_task6_validate.py",
    "tools/mem_leak_checker_task6_context.py",
    "tools/mem_leak_checker_task6_files.py",
    "tools/mem_leak_checker_task6_git_trust.py",
    "tools/mem_leak_checker_task6_host_qa.sh",
    "tools/mem_leak_checker_task6_manifest.py",
    "tools/mem_leak_checker_task6_output.py",
    "tools/mem_leak_checker_task6_publish.py",
    "tools/mem_leak_checker_task6_receipt.py",
    "tools/mem_leak_checker_task6_runner.py",
    "tools/mem_leak_checker_task6_schema.py",
    "tools/mem_leak_checker_task6_types.py",
    "tools/mem_leak_checker_scenarios/task-6.json",
    "tools/test_mem_leak_checker_task6_validate.py",
)


def bootstrap_verify() -> None:
    root = Path(__file__).resolve().parent.parent
    environment = {
        "GIT_CONFIG_GLOBAL": "/dev/null", "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_NO_REPLACE_OBJECTS": "1", "LC_ALL": "C",
        "PATH": "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
        "TMPDIR": "/tmp",
    }

    def git(*arguments: str) -> bytes:
        result = subprocess.run(
            ["/usr/bin/git", "-C", str(root), *arguments],
            capture_output=True, env=environment, check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode(errors="replace").strip())
        return result.stdout

    def stable_read(relative: str) -> bytes:
        nofollow = getattr(os, "O_NOFOLLOW", 0)
        directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | nofollow)
        try:
            parts = Path(relative).parts
            for component in parts[:-1]:
                child = os.open(component, os.O_RDONLY | os.O_DIRECTORY | nofollow, dir_fd=directory)
                os.close(directory)
                directory = child
            descriptor = os.open(parts[-1], os.O_RDONLY | os.O_NONBLOCK | nofollow, dir_fd=directory)
            try:
                before = os.fstat(descriptor)
                chunks: list[bytes] = []
                while chunk := os.read(descriptor, 65536):
                    chunks.append(chunk)
                after = os.fstat(descriptor)
                named = os.stat(parts[-1], dir_fd=directory, follow_symlinks=False)
                identity = (before.st_dev, before.st_ino, before.st_size)
                if not stat.S_ISREG(before.st_mode) or identity != (
                    after.st_dev, after.st_ino, after.st_size,
                ) or identity != (named.st_dev, named.st_ino, named.st_size):
                    raise RuntimeError(f"bootstrap descriptor drift: {relative}")
                content = b"".join(chunks)
                if len(content) != before.st_size:
                    raise RuntimeError(f"bootstrap size drift: {relative}")
                return content
            finally:
                os.close(descriptor)
        finally:
            os.close(directory)

    if git("rev-parse", "--show-toplevel").decode().strip() != str(root):
        raise RuntimeError("bootstrap repository identity mismatch")
    if git("for-each-ref", "refs/replace"):
        raise RuntimeError("bootstrap replace refs rejected")
    flags = git("ls-files", "-v", "--", *BOOTSTRAP_PATHS).splitlines()
    if len(flags) != len(BOOTSTRAP_PATHS) or any(line[:1] == b"S" or line[:1].islower() for line in flags):
        raise RuntimeError("bootstrap index flags rejected")
    head = git("rev-parse", "HEAD").decode().strip()
    for relative in BOOTSTRAP_PATHS:
        if stable_read(relative) != git("show", f"{head}:{relative}"):
            raise RuntimeError(f"bootstrap HEAD byte mismatch: {relative}")


if __name__ == "__main__":
    try:
        bootstrap_verify()
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from mem_leak_checker_task6_context import (
    derive_current_context,
    derive_context,
    production_context,
    validate_receipt_context,
)
from mem_leak_checker_task6_files import read_path
from mem_leak_checker_task6_schema import (
    canonical_json,
    fail,
    observation,
    parse_receipt_v2,
    parse_scenario_v1,
)
from mem_leak_checker_task6_types import (
    ContextSnapshot,
    ContractError,
    ExpectedContext,
    Observation,
    ReceiptDocument,
    ScenarioDocument,
)

__all__ = (
    "ContextSnapshot", "ContractError", "ExpectedContext", "Observation",
    "ReceiptDocument", "ScenarioDocument", "canonical_json", "derive_context",
    "observation", "parse_receipt_v2", "parse_scenario_v1",
    "validate_receipt_context",
)


def observations_from(
    directory: Path,
    commands: dict[str, str],
) -> tuple[tuple[str, Observation], ...]:
    values: list[tuple[str, Observation]] = []
    for kind in ("happy", "failure", "fatal"):
        exit_text = (directory / f"{kind}.exit").read_text().strip()
        if re.fullmatch(r"[0-9]+", exit_text) is None:
            raise fail(f"observations.{kind}.exit", "nonnegative decimal exit required")
        values.append((kind, observation(
            commands[kind], int(exit_text),
            (directory / f"{kind}.stdout").read_bytes(),
            (directory / f"{kind}.stderr").read_bytes(),
        )))
    return tuple(values)


def main() -> int:
    if len(sys.argv) == 4 and sys.argv[1] == "context" and sys.argv[2] == "--root":
        root = Path(sys.argv[3])
        expected = production_context(root)
        derive_current_context(expected)
        return 0
    receipt_mode = len(sys.argv) in {7, 9} and sys.argv[1] == "receipt" and sys.argv[3] == "--root" and sys.argv[5] == "--receiving-sha"
    if not receipt_mode:
        print(f"usage: {sys.argv[0]} context --root ROOT | receipt PATH --root ROOT --receiving-sha SHA [--observation-dir DIR]", file=sys.stderr)
        return 64
    root = Path(sys.argv[4])
    expected = production_context(root)
    receipt_path = Path(sys.argv[2])
    expected_receipt = expected.root / expected.evidence_path / f"task-6-post-integration-{sys.argv[6]}.json"
    if receipt_path != expected_receipt:
        raise fail("receipt.path", "exact evidence directory and receipt name required")
    snapshot = derive_context(expected, receipt_path.relative_to(root).as_posix())
    if snapshot.receiving_sha != sys.argv[6]:
        raise fail("receiving_sha", "current HEAD required")
    document = parse_receipt_v2(read_path(receipt_path))
    observed = None
    if len(sys.argv) == 9:
        if sys.argv[7] != "--observation-dir":
            return 64
        observed = observations_from(Path(sys.argv[8]), dict(snapshot.commands))
    validate_receipt_context(document, snapshot, observed)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ContractError, OSError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
