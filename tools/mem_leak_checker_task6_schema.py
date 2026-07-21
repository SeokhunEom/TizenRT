from __future__ import annotations

import json
import re
import shlex
from typing import Final

from mem_leak_checker_task6_manifest import (
    CASE_FIXTURES, RED_FIXTURE, SCHEMA_VERSION, TASK, THREAT_MODEL,
)
from mem_leak_checker_task6_output import observation
from mem_leak_checker_task6_types import (
    ContextSnapshot,
    ContractError,
    JsonValue,
    Observation,
    ReceiptDocument,
    RedScenario,
    ScenarioCase,
    ScenarioDocument,
)

QEMU_STATUS: Final = "deferred_unexecuted_baseline_link_failure"
VERDICT: Final = "host_scenarios_sealed_qemu_explicitly_deferred"
KINDS: Final = ("happy", "failure", "fatal")
SHA1_PATTERN: Final = re.compile(r"[0-9a-f]{40}\Z")
SHA256_PATTERN: Final = re.compile(r"[0-9a-f]{64}\Z")
FIXTURE_PATTERN: Final = re.compile(r"[a-z0-9_]+\Z")
FORBIDDEN_COMMAND: Final = frozenset(";&|<>$`(){}\\\n\r")


def fail(path: str, problem: str) -> ContractError:
    return ContractError(path=path, problem=problem)


def _pairs(pairs: list[tuple[str, JsonValue]]) -> dict[str, JsonValue]:
    result: dict[str, JsonValue] = {}
    for key, value in pairs:
        if key in result:
            raise fail("$", f"duplicate key {key!r}")
        result[key] = value
    return result


def canonical_json(value: JsonValue) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _load(raw: bytes) -> JsonValue:
    try:
        value: JsonValue = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise fail("$", "canonical UTF-8 JSON required") from error
    if raw != canonical_json(value):
        raise fail("$", "canonical sorted JSON with one trailing newline required")
    return value


def _mapping(value: JsonValue, path: str, keys: set[str]) -> dict[str, JsonValue]:
    if type(value) is not dict:
        raise fail(path, "object required")
    if set(value) != keys:
        raise fail(path, f"exact keys required: {sorted(keys)!r}")
    return value


def _string(value: JsonValue, path: str) -> str:
    if type(value) is not str or not value:
        raise fail(path, "non-empty string required")
    return value


def _text(value: JsonValue, path: str) -> str:
    if type(value) is not str:
        raise fail(path, "string required")
    try:
        value.encode("ascii")
    except UnicodeEncodeError as error:
        raise fail(path, "ASCII transcript required") from error
    return value


def _integer(value: JsonValue, path: str) -> int:
    if type(value) is not int:
        raise fail(path, "integer required")
    return value


def _boolean(value: JsonValue, path: str) -> bool:
    if type(value) is not bool:
        raise fail(path, "boolean required")
    return value


def _digest(value: JsonValue, path: str) -> str:
    digest = _string(value, path)
    if SHA256_PATTERN.fullmatch(digest) is None:
        raise fail(path, "lowercase SHA-256 required")
    return digest


def _strings(value: JsonValue, path: str, *, empty: bool = False) -> tuple[str, ...]:
    if type(value) is not list or (not empty and not value):
        raise fail(path, "array of strings required")
    strings = tuple(_string(item, f"{path}[]") for item in value)
    if len(set(strings)) != len(strings):
        raise fail(path, "entries must be unique")
    return strings


def _records(value: JsonValue, path: str) -> tuple[str, ...]:
    records = _strings(value, path)
    if any("\n" in item or not item.startswith("MLC_TASK6_") for item in records):
        raise fail(path, "single-line Task6 records required")
    return records


def command_argv(command: str, path: str = "command") -> tuple[str, ...]:
    if any(character in FORBIDDEN_COMMAND for character in command):
        raise fail(path, "shell metacharacters forbidden")
    try:
        argv = tuple(shlex.split(command, posix=True))
    except ValueError as error:
        raise fail(path, "canonical argv command required") from error
    if not argv or " ".join(argv) != command:
        raise fail(path, "canonical argv command required")
    return argv


def _fixture(value: JsonValue, path: str) -> str:
    fixture = _string(value, path)
    if FIXTURE_PATTERN.fullmatch(fixture) is None:
        raise fail(path, "canonical fixture name required")
    return fixture


def _parse_red(value: JsonValue) -> RedScenario:
    item = _mapping(value, "$.red", {"fixture", "command", "expected_exit", "expected_records"})
    fixture = _fixture(item["fixture"], "$.red.fixture")
    if fixture != RED_FIXTURE:
        raise fail("$.red.fixture", "exact canonical RED fixture required")
    command = _string(item["command"], "$.red.command")
    expected = "tools/mem_leak_checker_qa.sh red --task 6 --config qemu/tc_1m " f"--fixture {fixture}"
    if command != expected or _integer(item["expected_exit"], "$.red.expected_exit") != 86:
        raise fail("$.red", "canonical RED command and exit 86 required")
    command_argv(command, "$.red.command")
    return RedScenario(fixture, command, 86, _records(item["expected_records"], "$.red.expected_records"))


def _parse_case(value: JsonValue, index: int) -> ScenarioCase:
    path = f"$.scenarios[{index}]"
    item = _mapping(value, path, {"kind", "fixtures", "command", "expected_exit", "expected_records"})
    kind = _string(item["kind"], f"{path}.kind")
    try:
        canonical_fixtures = CASE_FIXTURES[kind]
    except KeyError as error:
        raise fail(f"{path}.kind", f"one of {KINDS!r} required") from error
    fixtures = _strings(item["fixtures"], f"{path}.fixtures")
    if any(FIXTURE_PATTERN.fullmatch(fixture) is None for fixture in fixtures):
        raise fail(f"{path}.fixtures", "canonical fixture names required")
    if fixtures != canonical_fixtures:
        raise fail(f"{path}.fixtures", "exact canonical fixture tuple required")
    joined = ",".join(fixtures)
    commands = {
        "happy": f"tools/mem_leak_checker_qa.sh qemu --task 6 --fixtures {joined} --post-commit",
        "failure": f"tools/mem_leak_checker_qa.sh qemu --task 6 --fixtures {joined} --repeat 500",
        "fatal": f"tools/mem_leak_checker_task6_qa.sh fatal --fixtures {joined}",
    }
    expected = commands[kind]
    if _string(item["command"], f"{path}.command") != expected:
        raise fail(f"{path}.command", "canonical command required")
    command_argv(expected, f"{path}.command")
    if _integer(item["expected_exit"], f"{path}.expected_exit") != 0:
        raise fail(f"{path}.expected_exit", "exit zero required")
    return ScenarioCase(kind, fixtures, expected, 0, _records(item["expected_records"], f"{path}.expected_records"))


def parse_scenario_v1(raw: bytes) -> ScenarioDocument:
    root = _mapping(_load(raw), "$", {"schema", "task", "qemu", "red", "scenarios"})
    if _integer(root["schema"], "$.schema") != 1 or _integer(root["task"], "$.task") != TASK:
        raise fail("$", "Task6 scenario schema version 1 required")
    if _string(root["qemu"], "$.qemu") != QEMU_STATUS:
        raise fail("$.qemu", "QEMU identity mismatch")
    values = root["scenarios"]
    if type(values) is not list or len(values) != len(KINDS):
        raise fail("$.scenarios", "exactly three scenarios required")
    scenarios = tuple(_parse_case(value, index) for index, value in enumerate(values))
    if tuple(item.kind for item in scenarios) != KINDS:
        raise fail("$.scenarios", "happy, failure, fatal order required")
    return ScenarioDocument(_parse_red(root["red"]), scenarios)


def _parse_observation(value: JsonValue, path: str) -> Observation:
    keys = {"command", "exit", "stdout", "stderr", "stdout_sha256", "stderr_sha256", "records_sha256"}
    item = _mapping(value, path, keys)
    parsed = observation(
        _string(item["command"], f"{path}.command"),
        _integer(item["exit"], f"{path}.exit"),
        _text(item["stdout"], f"{path}.stdout").encode("ascii"),
        _text(item["stderr"], f"{path}.stderr").encode("ascii"),
    )
    hashes = (
        _digest(item["stdout_sha256"], f"{path}.stdout_sha256"),
        _digest(item["stderr_sha256"], f"{path}.stderr_sha256"),
        _digest(item["records_sha256"], f"{path}.records_sha256"),
    )
    if hashes != (parsed.stdout_sha256, parsed.stderr_sha256, parsed.records_sha256):
        raise fail(path, "transcript digest mismatch")
    return parsed


def parse_receipt_v2(raw: bytes) -> ReceiptDocument:
    keys = {"schema_version", "task", "status", "qemu", "runtime_claim", "hardware_validation", "receiving_sha", "receiving_tree", "branch", "root", "worktree", "baseline_sha", "boulder", "normalized_plan_sha256", "scenario_sha256", "receiving_commit_source_sha256", "scenario_commands", "observations", "external_state_exclusions", "publication", "threat_model"}
    root = _mapping(_load(raw), "$", keys)
    if _integer(root["schema_version"], "$.schema_version") != SCHEMA_VERSION or _integer(root["task"], "$.task") != TASK:
        raise fail("$", "Task6 receipt schema version 2 required")
    if _string(root["status"], "$.status") != VERDICT or _string(root["qemu"], "$.qemu") != QEMU_STATUS:
        raise fail("$", "status or QEMU identity mismatch")
    if _boolean(root["runtime_claim"], "$.runtime_claim") or _string(root["hardware_validation"], "$.hardware_validation") != "skipped_by_user":
        raise fail("$", "runtime and hardware claims mismatch")
    sha = _string(root["receiving_sha"], "$.receiving_sha")
    tree = _string(root["receiving_tree"], "$.receiving_tree")
    if SHA1_PATTERN.fullmatch(sha) is None or SHA1_PATTERN.fullmatch(tree) is None:
        raise fail("$", "lowercase receiving SHA and tree required")
    boulder = _mapping(root["boulder"], "$.boulder", {"schema_version", "work_id", "plan", "session", "evidence_directory"})
    if _integer(boulder["schema_version"], "$.boulder.schema_version") != 2:
        raise fail("$.boulder.schema_version", "Boulder schema 2 required")
    commands = _mapping(root["scenario_commands"], "$.scenario_commands", {"red", *KINDS})
    observations = _mapping(root["observations"], "$.observations", set(KINDS))
    publication = _mapping(root["publication"], "$.publication", {"mode", "file_fsync", "directory_fsync", "immutable", "atomic_visibility"})
    for key in ("file_fsync", "directory_fsync", "immutable", "atomic_visibility"):
        _boolean(publication[key], f"$.publication.{key}")
    if publication != {"mode": "exclusive_final_inode_weaker_exfat", "file_fsync": True, "directory_fsync": True, "immutable": False, "atomic_visibility": False}:
        raise fail("$.publication", "publication contract mismatch")
    threat_model = _mapping(root["threat_model"], "$.threat_model", set(THREAT_MODEL))
    if threat_model != THREAT_MODEL:
        raise fail("$.threat_model", "Task6 cooperative threat model mismatch")
    snapshot = ContextSnapshot(
        root=_string(root["root"], "$.root"), branch=_string(root["branch"], "$.branch"),
        baseline_sha=_string(root["baseline_sha"], "$.baseline_sha"), receiving_sha=sha,
        receiving_tree=tree, work_id=_string(boulder["work_id"], "$.boulder.work_id"),
        plan_path=_string(boulder["plan"], "$.boulder.plan"), session_id=_string(boulder["session"], "$.boulder.session"),
        evidence_path=_string(boulder["evidence_directory"], "$.boulder.evidence_directory"),
        normalized_plan_sha256=_digest(root["normalized_plan_sha256"], "$.normalized_plan_sha256"),
        scenario_sha256=_digest(root["scenario_sha256"], "$.scenario_sha256"),
        source_sha256=_digest(root["receiving_commit_source_sha256"], "$.receiving_commit_source_sha256"),
        commands=tuple((kind, _string(commands[kind], f"$.scenario_commands.{kind}")) for kind in ("red", *KINDS)),
        external_state_exclusions=_strings(root["external_state_exclusions"], "$.external_state_exclusions", empty=True),
    )
    if _string(root["worktree"], "$.worktree") != snapshot.root:
        raise fail("$.worktree", "root and worktree must match")
    return ReceiptDocument(snapshot, tuple((kind, _parse_observation(observations[kind], f"$.observations.{kind}")) for kind in KINDS))
