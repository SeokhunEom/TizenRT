#!/usr/bin/env python3

from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_qemu_armv8m_workflow import HELLO_EXACT_ONCE_MARKERS, IMAGE_DIGEST, RUNTIME_TIMEOUT_SECONDS, analyze_workflow


class QemuArmv8mWorkflowContractTest(unittest.TestCase):
    def workflow(self) -> str:
        return (ROOT / ".github/workflows/qemu-armv8m.yml").read_text(encoding="utf-8")

    def test_workflow_satisfies_qemu_ci_contract(self) -> None:
        self.assertEqual([], analyze_workflow(self.workflow()))

    def test_rejects_missing_hello_execution_owner_marker(self) -> None:
        marker = HELLO_EXACT_ONCE_MARKERS[0]
        workflow = self.workflow().replace(f'              --expect-once "[{marker}] PASS"\n', "", 1)

        self.assertTrue(any(marker in error for error in analyze_workflow(workflow)))

    def test_rejects_mutable_runner_action_and_image(self) -> None:
        workflow = self.workflow().replace("ubuntu-24.04", "ubuntu-latest")
        workflow = workflow.replace("actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683", "actions/checkout@v4")
        workflow = workflow.replace(IMAGE_DIGEST, "tizenrt/tizenrt:latest")
        errors = analyze_workflow(workflow)
        self.assertTrue(any("runs-on" in error for error in errors), errors)
        self.assertTrue(any("full commit SHA" in error for error in errors), errors)
        self.assertTrue(any("mutable TizenRT image tag" in error for error in errors), errors)

    def test_rejects_missing_negative_marker_xip_check_and_masked_distclean(self) -> None:
        workflow = self.workflow().replace("--forbid-marker \"QEMU_APP1_STARTED\"", "", 1)
        workflow = workflow.replace("QEMU_LOAD_REJECT app1", "QEMU_LOAD_REJECT missing-app1", 1)
        workflow = workflow.replace("Verify generated XIP layout", "Verify generated layout", 1)
        workflow = workflow.replace("make distclean", "make distclean || true", 1)
        errors = analyze_workflow(workflow)
        self.assertTrue(any("forbid" in error for error in errors), errors)
        self.assertTrue(any("corrupt-app1" in error for error in errors), errors)
        self.assertTrue(any("layout checker" in error for error in errors), errors)
        self.assertTrue(any("distclean failure" in error for error in errors), errors)

    def test_rejects_incomplete_runtime_and_exception_matrices(self) -> None:
        workflow = self.workflow().replace("          - xip_all", "          - unsupported", 1)
        workflow = workflow.replace("          - name: qemu-fpu", "          - name: missing-qemu-fpu", 1)
        errors = analyze_workflow(workflow)
        self.assertTrue(any("positive runtime matrix" in error for error in errors), errors)
        self.assertTrue(any("exception-return compile" in error for error in errors), errors)

    def test_rejects_any_docker_run_with_another_digest(self) -> None:
        suffix = " \\" + "\n            bash -lc"
        replacement = "tizenrt/tizenrt@sha256:" + "0" * 64 + suffix
        workflow = self.workflow().replace(IMAGE_DIGEST + suffix, replacement, 1)
        self.assertTrue(any("image digest" in error for error in analyze_workflow(workflow)))

    def test_rejects_non_pinned_runner_in_any_job(self) -> None:
        workflow = self.workflow().replace("  qemu-negative:\n    name: negative-${{ matrix.case }}\n    runs-on: ubuntu-24.04", "  qemu-negative:\n    name: negative-${{ matrix.case }}\n    runs-on: ubuntu-22.04", 1)
        self.assertTrue(any("qemu-negative" in error for error in analyze_workflow(workflow)))

    def test_rejects_runtime_timeout_without_measured_margin(self) -> None:
        workflow = self.workflow().replace(
            f"--timeout {RUNTIME_TIMEOUT_SECONDS}",
            "--timeout 600",
            1,
        )
        self.assertTrue(any("runtime timeout" in error for error in analyze_workflow(workflow)))

    def test_rejects_duplicate_positive_and_negative_matrix_entries(self) -> None:
        workflow = self.workflow().replace("          - xip_all\n", "          - xip_all\n          - xip_all\n", 1)
        workflow = workflow.replace("          - case: corrupt-common\n            config: xip_all\n", "          - case: corrupt-common\n            config: xip_all\n          - case: corrupt-common\n            config: xip_all\n", 1)
        errors = analyze_workflow(workflow)
        self.assertTrue(any("positive runtime matrix" in error for error in errors), errors)
        self.assertTrue(any("negative runtime matrix" in error for error in errors), errors)

    def test_rejects_wrong_negative_case_config_mapping(self) -> None:
        workflow = self.workflow().replace("          - case: corrupt-common\n            config: xip_all", "          - case: corrupt-common\n            config: hello", 1)
        self.assertTrue(any("corrupt-common" in error and "xip_all" in error for error in analyze_workflow(workflow)))

    def test_rejects_negative_protocol_without_expect_reject_flag(self) -> None:
        workflow = self.workflow().replace("--expect-reject", "--not-expect", 1)
        self.assertTrue(any("corrupt-common" in error and "--expect-reject" in error for error in analyze_workflow(workflow)))

    def test_rejects_missing_xip_program_or_section_report(self) -> None:
        workflow = self.workflow().replace("readelf -lW build/output/bin/common_dbg", "readelf -h build/output/bin/common_dbg", 1)
        self.assertTrue(any("PT_LOAD" in error for error in analyze_workflow(workflow)))

    def test_rejects_layout_checker_without_lma_validation(self) -> None:
        checker = (ROOT / ".github/scripts/tests/qemu_armv8m_layout.py").read_text(encoding="utf-8")
        errors = analyze_workflow(self.workflow(), checker.replace("PT_LOAD LMA", "PT_LOAD missing-LMA", 1))
        self.assertTrue(any("VMA/LMA/file" in error for error in errors), errors)

    def test_rejects_missing_artifact_directory_creation(self) -> None:
        workflow = self.workflow().replace("mkdir -p \"${CI_ARTIFACT_DIR}\"", "", 1)
        self.assertTrue(any("artifact directory" in error for error in analyze_workflow(workflow)))

    def test_rejects_missing_os_and_workflow_path_filters(self) -> None:
        workflow = self.workflow().replace('      - "os/**"\n', "", 1)
        workflow = workflow.replace('      - ".github/workflows/qemu-armv8m.yml"\n', "", 1)
        errors = analyze_workflow(workflow)
        self.assertTrue(any("os/**" in error for error in errors), errors)
        self.assertTrue(any("qemu-armv8m.yml" in error for error in errors), errors)

    def test_rejects_inverted_exception_define(self) -> None:
        workflow = self.workflow().replace("-DTEST_EXPECT_QEMU=1", "-DTEST_EXPECT_QEMU=0", 1)
        self.assertTrue(any("exception-return compile" in error for error in analyze_workflow(workflow)))

    def test_rejects_wrong_full_sha_checkout_action_identity(self) -> None:
        workflow = self.workflow().replace("actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683", "actions/cache@11bd71901bbe5b1630ceea73d27597364c9af683", 1)
        self.assertTrue(any("checkout action" in error for error in analyze_workflow(workflow)))

    def test_rejects_oversized_app1_case_that_can_stop_at_crc(self) -> None:
        crc_refreshed = self.workflow().replace(
            "printf '\\000\\000\\020\\000' | dd of=\"${CI_ARTIFACT_DIR}/oversized-app1\" bs=1 seek=8 conv=notrunc status=none",
            "printf '\\000\\000\\020\\000' | dd of=\"${CI_ARTIFACT_DIR}/oversized-app1\" bs=1 seek=9 conv=notrunc status=none\n"
            "              dd if=\"${CI_ARTIFACT_DIR}/oversized-app1\" of=\"${CI_ARTIFACT_DIR}/oversized-app1.payload\" bs=1 skip=4 status=none\n"
            "              python3 os/tools/mkchecksum.py \"${CI_ARTIFACT_DIR}/oversized-app1.payload\"\n"
            "              mv \"${CI_ARTIFACT_DIR}/oversized-app1.payload\" \"${CI_ARTIFACT_DIR}/oversized-app1\"",
            1,
        )
        exact_rejection = crc_refreshed.replace(
            '--app1 "${CI_ARTIFACT_DIR}/oversized-app1" --expect-reject "QEMU_LOAD_REJECT app1"',
            '--app1 "${CI_ARTIFACT_DIR}/oversized-app1" --expect-reject "QEMU_LOAD_REJECT app1 size"',
            1,
        )
        mutations = {
            "size field offset": exact_rejection.replace("seek=9", "seek=8", 1),
            "CRC refresh": exact_rejection.replace("python3 os/tools/mkchecksum.py", "true #", 1),
            "size rejection marker": exact_rejection.replace("QEMU_LOAD_REJECT app1 size", "QEMU_LOAD_REJECT app1", 1),
        }
        for diagnostic, mutated in mutations.items():
            with self.subTest(diagnostic=diagnostic):
                self.assertTrue(any(diagnostic in error for error in analyze_workflow(mutated)))


if __name__ == "__main__":
    unittest.main()
