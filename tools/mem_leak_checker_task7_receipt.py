#!/usr/bin/env python3
import json
import os
import pathlib
import re
import stat
import sys

EXPECTED_KEYS = frozenset({
    "authoritative", "baseline_sha", "commands", "manifest_equation",
    "patch_sha256", "plan_sha256", "qemu", "receiving_sha",
    "receiving_tree", "runtime_claim", "scenario_sha256", "schema",
    "source_manifest_sha256", "task",
})
SHA256 = re.compile(r"^[0-9a-f]{64}$")
GIT_OBJECT = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")


def scenario_commands():
    scenario = pathlib.Path(__file__).with_name("mem_leak_checker_scenarios") / \
        "task-7.json"
    document = json.loads(scenario.read_text(encoding="utf-8"))
    return {
        document["happy"][0]["command"]: "happy",
        document["failure"][0]["command"]: "failure",
    }


def reject_duplicate_keys(pairs):
    document = {}
    for key, value in pairs:
        if key in document:
            raise ValueError("duplicate receipt key")
        document[key] = value
    return document


def parse_receipt(payload):
    try:
        document = json.loads(payload.decode("utf-8"),
                              object_pairs_hook=reject_duplicate_keys)
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise RuntimeError("receipt is not valid JSON") from error
    if not isinstance(document, dict) or set(document) != EXPECTED_KEYS:
        raise RuntimeError("receipt schema keys mismatch")
    if document["schema"] != 2 or document["task"] != 7:
        raise RuntimeError("receipt schema or task mismatch")
    if document["authoritative"] is not True or \
            document["runtime_claim"] is not False:
        raise RuntimeError("receipt authority mismatch")
    if document["qemu"] != "deferred_unexecuted_baseline_link_failure" or \
            document["manifest_equation"] != \
            "allocated_count=candidate_count+exclusion_count":
        raise RuntimeError("receipt policy mismatch")
    for field in ("patch_sha256", "plan_sha256", "scenario_sha256",
                  "source_manifest_sha256"):
        value = document[field]
        if not isinstance(value, str) or SHA256.fullmatch(value) is None:
            raise RuntimeError(f"receipt {field} is not a SHA-256")
    for field in ("baseline_sha", "receiving_sha", "receiving_tree"):
        value = document[field]
        if not isinstance(value, str) or GIT_OBJECT.fullmatch(value) is None:
            raise RuntimeError(f"receipt {field} is not a Git object ID")
    commands = document["commands"]
    if not isinstance(commands, list) or len(commands) != 2:
        raise RuntimeError("receipt command count mismatch")
    expected_commands = scenario_commands()
    kinds = set()
    for command in commands:
        if not isinstance(command, dict) or set(command) != {
                "command", "exit", "kind", "stdout_sha256"}:
            raise RuntimeError("receipt command schema mismatch")
        if not isinstance(command["command"], str) or \
                command["command"] not in expected_commands or \
                expected_commands[command["command"]] != command["kind"]:
            raise RuntimeError("receipt command route mismatch")
        if command["exit"] != 0 or command["kind"] not in ("happy", "failure"):
            raise RuntimeError("receipt command result mismatch")
        if command["kind"] in kinds:
            raise RuntimeError("receipt command kinds are not unique")
        kinds.add(command["kind"])
        if not isinstance(command["stdout_sha256"], str) or \
                SHA256.fullmatch(command["stdout_sha256"]) is None:
            raise RuntimeError("receipt command transcript is not a SHA-256")
    if kinds != {"happy", "failure"}:
        raise RuntimeError("receipt command kinds mismatch")
    return document


def nofollow_flag():
    if not hasattr(os, "O_NOFOLLOW"):
        raise RuntimeError("O_NOFOLLOW unsupported")
    return os.O_NOFOLLOW


def read_regular(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | nofollow_flag())
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise RuntimeError("receipt is not a regular file")
        chunks = []
        while True:
            chunk = os.read(descriptor, 65536)
            if not chunk:
                break
            chunks.append(chunk)
        return b"".join(chunks)
    finally:
        os.close(descriptor)


def publish(path, payload):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                         nofollow_flag(), 0o600)
    try:
        offset = 0
        while offset < len(payload):
            written = os.write(descriptor, payload[offset:])
            if written <= 0:
                raise RuntimeError("short receipt write")
            offset += written
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    directory = os.open(os.path.dirname(path), os.O_RDONLY |
                        os.O_DIRECTORY | nofollow_flag())
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("publish", "validate"):
        return 64
    path = sys.argv[2]
    payload = sys.stdin.buffer.read()
    if sys.argv[1] == "publish":
        publish(path, payload)
    else:
        actual = parse_receipt(read_regular(path))
        expected = parse_receipt(payload)
        if actual != expected:
            raise RuntimeError("receipt replay mismatch")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
