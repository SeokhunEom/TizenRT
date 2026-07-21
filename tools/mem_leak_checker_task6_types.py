from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import TypeAlias

JsonScalar: TypeAlias = None | bool | int | float | str
JsonValue: TypeAlias = JsonScalar | list["JsonValue"] | dict[str, "JsonValue"]


@dataclass(frozen=True, slots=True)
class ContractError(Exception):
    path: str
    problem: str

    def __str__(self) -> str:
        return f"{self.path}: {self.problem}"


@dataclass(frozen=True, slots=True)
class ExpectedContext:
    root: Path
    branch: str
    baseline_sha: str
    work_id: str
    plan_path: str
    session_id: str
    evidence_path: str
    normalized_plan_sha256: str


@dataclass(frozen=True, slots=True)
class RedScenario:
    fixture: str
    command: str
    expected_exit: int
    expected_records: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class ScenarioCase:
    kind: str
    fixtures: tuple[str, ...]
    command: str
    expected_exit: int
    expected_records: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class ScenarioDocument:
    red: RedScenario
    scenarios: tuple[ScenarioCase, ...]


@dataclass(frozen=True, slots=True)
class Observation:
    command: str
    exit_code: int
    stdout: str
    stderr: str
    stdout_sha256: str
    stderr_sha256: str
    records_sha256: str


@dataclass(frozen=True, slots=True)
class ContextSnapshot:
    root: str
    branch: str
    baseline_sha: str
    receiving_sha: str
    receiving_tree: str
    work_id: str
    plan_path: str
    session_id: str
    evidence_path: str
    normalized_plan_sha256: str
    scenario_sha256: str
    source_sha256: str
    commands: tuple[tuple[str, str], ...]
    external_state_exclusions: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class ReceiptDocument:
    snapshot: ContextSnapshot
    observations: tuple[tuple[str, Observation], ...]
