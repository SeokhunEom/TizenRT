from __future__ import annotations

import hashlib
import json
from typing import Final

from mem_leak_checker_task6_types import ContractError, Observation

DARWIN_LD_WARNING: Final = (
    b"ld: warning: /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib/"
    b"libSystem.tbd, ignoring unexpected dylib text stub file"
)


def reject(path: str, problem: str) -> ContractError:
    return ContractError(path=path, problem=problem)


def observation(command: str, exit_code: int, stdout: bytes, stderr: bytes) -> Observation:
    if stdout and (
        not stdout.endswith(b"\n")
        or any(not line.startswith(b"MLC_TASK6_") for line in stdout.splitlines())
    ):
        raise reject("stdout", "Task6 records only required")
    if stderr and (
        not stderr.endswith(b"\n")
        or any(line != DARWIN_LD_WARNING for line in stderr.splitlines())
    ):
        raise reject("stderr", "empty or deterministic linker warnings required")
    try:
        stdout_text = stdout.decode("ascii")
        stderr_text = stderr.decode("ascii")
    except UnicodeDecodeError as error:
        raise reject("transcript", "ASCII Task6 transcript required") from error
    records = stdout_text.splitlines()
    encoded_records = (
        json.dumps(records, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode()
    return Observation(
        command, exit_code, stdout_text, stderr_text,
        hashlib.sha256(stdout).hexdigest(), hashlib.sha256(stderr).hexdigest(),
        hashlib.sha256(encoded_records).hexdigest(),
    )
