#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Final


IMAGE_DIGEST: Final = "tizenrt/tizenrt@sha256:678bc19455484d414031d322fd62853134b1353e9b61d466c0db9e85696a01b3"
CHECKOUT_ACTION: Final = "actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683"
UPLOAD_ACTION: Final = "actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08"
RUNTIME_TIMEOUT_SECONDS: Final = 1200
FULL_SHA: Final = re.compile(r"^[0-9a-f]{40}$")
POSITIVES: Final = {"hello", "loadable_all", "loadable_apps", "xip_all"}
NEGATIVE_CONFIGS: Final = {
    "corrupt-common": "xip_all",
    "omitted-common": "xip_all",
    "corrupt-app1": "loadable_all",
    "oversized-app1": "loadable_all",
}
NEGATIVE_MARKERS: Final = {
    "corrupt-common": "binary_manager_load: Invalid Header data, name : common",
    "omitted-common": "binary_manager_load: Invalid Header data, name : common",
    "corrupt-app1": "binary_manager_load: Invalid Header data, name : app1",
    "oversized-app1": "binary_manager_load: Invalid Header data, name : app1",
}
NEGATIVE_FORBIDDEN_MARKERS: Final = {
    "corrupt-common": "binary_manager_load: common Header Checking Success",
    "omitted-common": "binary_manager_load: common Header Checking Success",
    "corrupt-app1": "binary_manager_load: app1 Header Checking Success",
    "oversized-app1": "binary_manager_load: app1 Header Checking Success",
}
REQUIRED_FILTERS: Final = {
    ".github/scripts/**",
    ".github/workflows/qemu-armv8m.yml",
    "apps/examples/testcase/**",
    "build/configs/qemu-armv8m/**",
    "loadable_apps/**",
    "framework/**",
    "os/**",
    "os/arch/arm/src/armv8-m/**",
    "os/board/qemu-armv8m/**",
    "os/tools/**",
}
REQUIRED_ARTIFACTS: Final = {
    "build.log",
    "defconfig.sha256",
    "image-digest.txt",
    "qemu-machine-help.txt",
    "qemu-package-version.txt",
    "qemu-version.txt",
    "result.json",
    "serial.log",
}
XIP_COMMANDS: Final = (
    "readelf -lW build/output/bin/common_dbg",
    "readelf -SW build/output/bin/common_dbg",
    "readelf -lW build/output/bin/common_1_dbg",
    "readelf -SW build/output/bin/common_1_dbg",
    "readelf -lW build/output/bin/app1_dbg",
    "readelf -SW build/output/bin/app1_dbg",
    "readelf -lW build/output/bin/app1_1_dbg",
    "readelf -SW build/output/bin/app1_1_dbg",
    "--common-ld build/output/bin/common_0.ld --common-artifact build/output/bin/common_dbg",
    "--common-alt-ld build/output/bin/common_1.ld --common-alt-artifact build/output/bin/common_1_dbg",
    "--app1-ld build/output/bin/app1_0.ld --app1-artifact build/output/bin/app1_dbg",
    "--app1-alt-ld build/output/bin/app1_1.ld --app1-alt-artifact build/output/bin/app1_1_dbg",
    "xip-layout-report.json",
)
LAYOUT_CHECKS: Final = ("PT_LOAD VMA", "PT_LOAD LMA", "PT_LOAD file extent", "allocated section")
HELLO_EXACT_ONCE_MARKERS: Final = (
    "tc_sched_sched_foreach",
    "signal_findaction_null_main",
    "tc_clock_clock_conversion",
    "tc_timer_timer_deleteall",
    "tc_semaphore_kernel",
    "tc_pthread_kernel",
    "tc_irq_kernel",
    "tc_sched_kernel_affinity",
    "tc_group_group_signal",
    "tc_sched_kernel_state",
    "tc_signal_kernel_pendingset",
    "tc_environ_kernel",
    "tc_errno_kernel",
    "tc_mqueue_kernel",
    "tc_procfs_kernel",
    "tc_pipe_kernel",
    "tc_vfs_kernel",
    "tc_termios_tcsetattr_tcgetattr",
    "tc_kmm_heap_negative_index",
    "tc_kmm_heap_zero_index",
    "tc_kmm_heap_upper_bound_index",
)
EXCEPTION_DEFINES: Final = {
    "qemu-non-fpu": frozenset({"-DCONFIG_ARCH_CHIP_QEMU_ARMV8M=1", "-DTEST_EXPECT_QEMU=1", "-DTEST_EXPECT_FPU=0", "-DTEST_EXPECT_BASE=0xffffffe1UL", "-DTEST_EXPECT_HANDLER=0xfffffff1UL", "-DTEST_EXPECT_PRIVTHR=0xfffffff9UL", "-DTEST_EXPECT_UNPRIVTHR=0xfffffffdUL"}),
    "qemu-fpu": frozenset({"-DCONFIG_ARCH_CHIP_QEMU_ARMV8M=1", "-DCONFIG_ARCH_FPU=1", "-DCONFIG_ARM_CMNVECTOR=1", "-DTEST_EXPECT_QEMU=1", "-DTEST_EXPECT_FPU=1", "-DTEST_EXPECT_BASE=0xffffffe1UL", "-DTEST_EXPECT_HANDLER=0xfffffff1UL", "-DTEST_EXPECT_PRIVTHR=0xffffffe9UL", "-DTEST_EXPECT_UNPRIVTHR=0xffffffedUL"}),
    "non-qemu-non-fpu": frozenset({"-DTEST_EXPECT_QEMU=0", "-DTEST_EXPECT_FPU=0", "-DTEST_EXPECT_BASE=0xffffffa0UL", "-DTEST_EXPECT_HANDLER=0xffffffb0UL", "-DTEST_EXPECT_PRIVTHR=0xffffffb8UL", "-DTEST_EXPECT_UNPRIVTHR=0xffffffbcUL"}),
    "non-qemu-fpu": frozenset({"-DCONFIG_ARCH_FPU=1", "-DCONFIG_ARM_CMNVECTOR=1", "-DTEST_EXPECT_QEMU=0", "-DTEST_EXPECT_FPU=1", "-DTEST_EXPECT_BASE=0xffffffa0UL", "-DTEST_EXPECT_HANDLER=0xffffffb0UL", "-DTEST_EXPECT_PRIVTHR=0xffffffa8UL", "-DTEST_EXPECT_UNPRIVTHR=0xffffffacUL"}),
}


def job_blocks(workflow: str) -> dict[str, str]:
    marker = "\njobs:\n"
    start = workflow.find(marker)
    if start < 0:
        return {}
    body = workflow[start + len(marker):]
    matches = list(re.finditer(r"^  ([A-Za-z0-9_-]+):\n", body, re.MULTILINE))
    blocks: dict[str, str] = {}
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(body)
        blocks[match.group(1)] = body[match.end():end]
    return blocks


def matrix_values(block: str, key: str) -> list[str]:
    values = re.search(rf"^        {re.escape(key)}:\n(?P<values>(?:^          - [A-Za-z0-9_-]+\n)+)", block, re.MULTILINE)
    if values is None:
        return []
    return re.findall(r"^          - ([A-Za-z0-9_-]+)$", values.group("values"), re.MULTILINE)


def negative_entries(block: str) -> list[tuple[str, str]]:
    return re.findall(r"^          - case: ([A-Za-z0-9_-]+)\n            config: ([A-Za-z0-9_-]+)$", block, re.MULTILINE)


def negative_case_block(block: str, name: str) -> str:
    marker = f"            {name})\n"
    start = block.find(marker)
    if start < 0:
        return ""
    after = start + len(marker)
    endings = [block.find(f"            {case})\n", after) for case in NEGATIVE_CONFIGS]
    endings.append(block.find("          esac", after))
    end = min(position for position in endings if position >= 0)
    return block[after:end]


def docker_run_images(workflow: str) -> list[str]:
    images: list[str] = []
    for match in re.finditer(r"\bdocker run\b", workflow):
        command = workflow[match.end():match.end() + 2048]
        image = re.search(r"(?m)^[ \t]+(tizenrt/tizenrt[^ \t\r\n]+)[ \t]*\\?[ \t]*$", command)
        images.append("" if image is None else image.group(1))
    return images


def exception_entries(block: str) -> list[tuple[str, frozenset[str]]]:
    entries: list[tuple[str, frozenset[str]]] = []
    pattern = re.compile(r"^          - name: ([A-Za-z0-9_-]+)\n            defines: >-\n(?P<defines>(?:^              .+\n)+)", re.MULTILINE)
    for match in pattern.finditer(block):
        entries.append((match.group(1), frozenset(match.group("defines").split())))
    return entries


def analyze_workflow(workflow: str, layout_checker: str | None = None) -> list[str]:
    errors: list[str] = []
    jobs = job_blocks(workflow)
    if not jobs:
        return ["workflow must define jobs"]
    for name, block in jobs.items():
        runners = re.findall(r"^    runs-on: ([^\n]+)$", block, re.MULTILINE)
        if runners != ["ubuntu-24.04"]:
            errors.append(f"{name} runs-on must be exactly ubuntu-24.04")

    actions = re.findall(r"^\s*uses:\s*([^\s]+)\s*$", workflow, re.MULTILINE)
    for action in actions:
        if "@" not in action or FULL_SHA.fullmatch(action.rsplit("@", 1)[1]) is None:
            errors.append(f"action must use a full commit SHA: {action}")
    checkouts = [action for action in actions if action.startswith("actions/checkout@")]
    if checkouts != [CHECKOUT_ACTION] * len(jobs):
        errors.append(f"checkout action identity/ref must be {CHECKOUT_ACTION} in every job")
    uploads = [action for action in actions if action.startswith("actions/upload-artifact@")]
    if uploads != [UPLOAD_ACTION, UPLOAD_ACTION]:
        errors.append(f"upload-artifact action identity/ref must be {UPLOAD_ACTION} for both artifact jobs")

    pulls = re.findall(r"(?m)^\s*run:\s+docker pull\s+(tizenrt/tizenrt[^\s]+)\s*$", workflow)
    runs = docker_run_images(workflow)
    images = [*pulls, *runs]
    if not pulls or not runs:
        errors.append("workflow must include Docker pull and run image digest commands")
    for image in images:
        if image != IMAGE_DIGEST:
            errors.append(f"docker pull/run image digest must equal {IMAGE_DIGEST}: {image or 'missing'}")
    if any("@sha256:" not in image for image in images):
        errors.append("workflow must not use a mutable TizenRT image tag")

    if "make distclean ||" in workflow or "make distclean;" in workflow:
        errors.append("distclean failure must not be masked")
    if "if [ -f .config ]; then" not in workflow or "make distclean" not in workflow:
        errors.append("distclean must run only when .config exists")
    for filter_path in REQUIRED_FILTERS:
        if workflow.count(filter_path) != 2:
            errors.append(f"path filters must include {filter_path} for push and pull requests")

    positive = jobs.get("qemu-positive", "")
    configs = matrix_values(positive, "config")
    if len(configs) != len(POSITIVES) or set(configs) != POSITIVES:
        errors.append("positive runtime matrix must contain exactly four unique supported configurations")
    runner = positive.find("--config \"${{ matrix.config }}\"")
    mkdir = positive.find("mkdir -p \"${CI_ARTIFACT_DIR}\"")
    if runner < 0:
        errors.append("positive runtime matrix must invoke the QEMU runner")
    if mkdir < 0 or runner < 0 or mkdir >= runner:
        errors.append("per-config artifact directory must exist before runner preflight")
    for artifact in REQUIRED_ARTIFACTS:
        if artifact not in positive:
            errors.append(f"per-config artifact is missing {artifact}")
    if re.findall(r"--timeout ([0-9]+)", positive) != [str(RUNTIME_TIMEOUT_SECONDS)]:
        errors.append(f"positive runtime timeout must be exactly {RUNTIME_TIMEOUT_SECONDS} seconds")
    if 'if [[ "${{ matrix.config }}" == "hello" ]]' not in positive:
        errors.append("exact-once migrated markers must be scoped to the hello configuration")
    for marker in HELLO_EXACT_ONCE_MARKERS:
        if positive.count(f'--expect-once "[{marker}] PASS"') != 1:
            errors.append(f"hello runtime must require exactly one [{marker}] PASS marker")

    xip = positive.find("Verify generated XIP layout")
    build = positive.find("Build qemu-armv8m")
    if xip < 0 or build < 0 or runner < 0 or build > xip or xip > runner:
        errors.append("xip_all layout checker must run after build and before QEMU")
    for command in XIP_COMMANDS:
        if command not in positive:
            errors.append(f"xip PT_LOAD VMA/LMA/file and section evidence is missing {command}")
    checker = layout_checker
    if checker is None:
        checker = Path(__file__).with_name("qemu_armv8m_layout.py").read_text(encoding="utf-8")
    if any(check not in checker for check in LAYOUT_CHECKS):
        errors.append("Task 9 layout checker must validate PT_LOAD VMA/LMA/file plus allocated sections")

    negative = jobs.get("qemu-negative", "")
    if re.findall(r"--timeout ([0-9]+)", negative) != [str(RUNTIME_TIMEOUT_SECONDS)] * len(NEGATIVE_CONFIGS):
        errors.append(f"every negative runtime timeout must be exactly {RUNTIME_TIMEOUT_SECONDS} seconds")
    entries = negative_entries(negative)
    expected_entries = set(NEGATIVE_CONFIGS.items())
    if len(entries) != len(expected_entries) or set(entries) != expected_entries:
        errors.append("negative runtime matrix must contain exactly four unique named package cases")
    for case, config in NEGATIVE_CONFIGS.items():
        if (case, config) not in entries:
            errors.append(f"negative matrix {case} must use {config}")
        case_body = negative_case_block(negative, case)
        marker = NEGATIVE_MARKERS[case]
        if f"--expect-reject \"{marker}\"" not in case_body:
            errors.append(f"negative case {case} must use --expect-reject \"{marker}\"")
        forbidden_marker = NEGATIVE_FORBIDDEN_MARKERS[case]
        if f'--forbid-marker "{forbidden_marker}"' not in case_body:
            errors.append(f"negative case {case} must forbid {forbidden_marker}")
    oversized = negative_case_block(negative, "oversized-app1")
    if 'bs=1 seek=9 conv=notrunc status=none' not in oversized:
        errors.append("oversized-app1 must write the bin_size field offset")
    if 'bs=1 skip=4 status=none' not in oversized or "python3 os/tools/mkchecksum.py" not in oversized:
        errors.append("oversized-app1 must perform a CRC refresh after changing metadata")
    if '--expect-reject "binary_manager_load: Invalid Header data, name : app1"' not in oversized:
        errors.append("oversized-app1 must require the app1 rejection marker")

    exception = jobs.get("exception-return-compile", "")
    entries = exception_entries(exception)
    if len(entries) != len(EXCEPTION_DEFINES) or dict(entries) != EXCEPTION_DEFINES or "test_exc_return_values.c" not in exception or "-fsyntax-only" not in exception:
        errors.append("exception-return compile matrix must preserve exact QEMU/non-QEMU by FPU semantics")
    if "dpkg-query -W" not in workflow or "mps2-an505" not in workflow:
        errors.append("workflow must record QEMU package version and mps2-an505 support")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("workflow", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    errors = analyze_workflow(args.workflow.read_text(encoding="utf-8"))
    report = {"ok": not errors, "errors": errors, "workflow": str(args.workflow)}
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
