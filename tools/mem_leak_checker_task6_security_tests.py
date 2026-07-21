from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from threading import Barrier
import copy
import json
import os
from pathlib import Path

from mem_leak_checker_task6_publish import publish
from mem_leak_checker_task6_output import DARWIN_LD_WARNING
from mem_leak_checker_task6_schema import canonical_json, observation, parse_scenario_v1
from mem_leak_checker_task6_types import ContractError, JsonValue
from mem_leak_checker_task6_validate_test_support import scenario_value


def _reject_scenario(value: dict[str, JsonValue]) -> None:
    try:
        parse_scenario_v1(canonical_json(value))
    except ContractError:
        return
    raise AssertionError("hostile scenario command accepted")


def _reject_observation(stdout: bytes, stderr: bytes) -> None:
    try:
        observation("command", 0, stdout, stderr)
    except ContractError:
        return
    raise AssertionError("misleading execution output accepted")


def _reject_publish(path: Path, encoded: bytes) -> None:
    try:
        publish(path, encoded)
    except (ContractError, OSError):
        return
    raise AssertionError("unsafe publication target accepted")


def _fixture_vocabulary_negatives() -> int:
    path = Path(__file__).parent / "mem_leak_checker_scenarios/task-6.json"
    base: dict[str, JsonValue] = json.loads(path.read_text())
    red_values = (
        "unknown_fixture",
        "mlc_try_heap_fresh_accounting",
        "mlc_domain_pin_production_path_extra_fixture",
        "mlc_try_heap_fresh_accounting_then_mlc_domain_pin_production_path",
    )
    for fixture in red_values:
        mutant = copy.deepcopy(base)
        mutant["red"]["fixture"] = fixture
        mutant["red"]["command"] = (
            "tools/mem_leak_checker_qa.sh red --task 6 --config qemu/tc_1m "
            f"--fixture {fixture}"
        )
        _reject_scenario(mutant)
    wrong = (
        "mlc_domain_unload_churn", "mlc_heap_release_ownership_fatal",
        "mlc_domain_pin_production_path",
    )
    for index, case in enumerate(base["scenarios"]):
        fixtures = tuple(case["fixtures"])
        mutations = (
            ("unknown_fixture", *fixtures[1:]),
            (wrong[index], *fixtures[1:]),
            tuple(reversed(fixtures)),
            (*fixtures, wrong[index]),
        )
        for values in mutations:
            mutant = copy.deepcopy(base)
            item = mutant["scenarios"][index]
            item["fixtures"] = list(values)
            joined = ",".join(values)
            match item["kind"]:
                case "happy":
                    item["command"] = f"tools/mem_leak_checker_qa.sh qemu --task 6 --fixtures {joined} --post-commit"
                case "failure":
                    item["command"] = f"tools/mem_leak_checker_qa.sh qemu --task 6 --fixtures {joined} --repeat 500"
                case "fatal":
                    item["command"] = f"tools/mem_leak_checker_task6_qa.sh fatal --fixtures {joined}"
                case unreachable:
                    raise AssertionError(f"unexpected fixture kind: {unreachable}")
            _reject_scenario(mutant)
    return 16


def test_security_boundaries(parent: Path) -> int:
    production = parse_scenario_v1(
        (Path(__file__).parent / "mem_leak_checker_scenarios/task-6.json").read_bytes()
    )
    happy = production.scenarios[0]
    assert happy.command.endswith(" --post-commit")
    assert happy.expected_records[-1].endswith("post_commit=true authoritative=true")
    vocabulary_cases = _fixture_vocabulary_negatives()
    value = scenario_value()
    hostile = copy.deepcopy(value)
    hostile["red"]["fixture"] = "pin;touch_pwned"
    hostile["red"]["command"] = (
        "tools/mem_leak_checker_qa.sh red --task 6 --config qemu/tc_1m "
        "--fixture pin;touch_pwned"
    )
    _reject_scenario(hostile)
    hostile = copy.deepcopy(value)
    hostile["scenarios"][0]["fixtures"] = ["happy$(touch_pwned)"]
    hostile["scenarios"][0]["command"] = (
        "tools/mem_leak_checker_qa.sh qemu --task 6 --fixtures "
        "happy$(touch_pwned) --post-commit"
    )
    _reject_scenario(hostile)
    _reject_observation(b"build succeeded\nMLC_TASK6_OK status=PASS\n", b"")
    _reject_observation(b"MLC_TASK6_OK status=PASS\n", b"misleading warning\n")
    observation(
        "command", 0, b"MLC_TASK6_OK status=PASS\n",
        DARWIN_LD_WARNING + b"\n" + DARWIN_LD_WARNING + b"\n",
    )
    count = 6 + vocabulary_cases

    publication = parent / "publication"
    path = publication / "receipt.json"
    encoded = b'{"sealed":true}\n'
    publish(path, encoded)
    assert path.read_bytes() == encoded and stat_mode(path) == 0o600
    publish(path, encoded)
    _reject_publish(path, b"different\n")
    count += 3
    path.unlink()
    target = publication / "target"
    target.write_bytes(encoded)
    path.symlink_to(target)
    _reject_publish(path, encoded)
    path.unlink()
    os.mkfifo(path)
    _reject_publish(path, encoded)
    path.unlink()
    count += 2
    for iteration in range(64):
        concurrent = publication / f"concurrent-{iteration}.json"
        barrier = Barrier(2)

        def publish_after_barrier(_: int) -> None:
            barrier.wait()
            publish(concurrent, encoded)

        with ThreadPoolExecutor(max_workers=2) as executor:
            results = tuple(executor.map(publish_after_barrier, range(2)))
        assert results == (None, None) and concurrent.read_bytes() == encoded
    count += 1
    for fault in ("partial-write", "file-fsync", "directory-fsync"):
        fault_path = publication / f"{fault}.json"
        os.environ["MLC_TASK6_PUBLISH_FAULT"] = fault
        _reject_publish(fault_path, encoded)
        os.environ.pop("MLC_TASK6_PUBLISH_FAULT")
        assert not fault_path.exists()
        count += 1
    return count


def stat_mode(path: Path) -> int:
    return path.stat().st_mode & 0o777
