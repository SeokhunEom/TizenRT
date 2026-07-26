from __future__ import annotations

import dataclasses
import unittest

from qemu_armv8m_kernel_tc_test_support import RunnerHarness


def network_child(mode: str = "pass") -> str:
    return f"""
import sys
import time

mode = {mode!r}
attempt = 0
sys.stdout.write("TASH>>")
sys.stdout.flush()
while True:
    command = sys.stdin.readline()
    if not command:
        break
    command = command.strip()
    if command.startswith("sleep "):
        sys.stdout.write("TASH>>")
    elif command == "help":
        sys.stdout.write("ifconfig ifdown ifup ping ping6 netmon net_stats netdb network_tc kernel_tc\\nTASH>>")
    elif command == "ifdown eth0":
        if attempt:
            sys.stdout.write("retry-network\\n")
        sys.stdout.write("ifdown eth0...OK\\nTASH>>")
    elif command == "ifup eth0":
        sys.stdout.write("ifup eth0...OK\\nTASH>>")
    elif command == "ifconfig eth0 dhcp":
        if mode == "dhcp-fail":
            sys.stdout.write("get IP address fail\\nTASH>>")
        else:
            sys.stdout.write("get IP address 10.0.2.15\\nTASH>>")
    elif command == "ifconfig eth0 fec0::15":
        sys.stdout.write("Host IP: fec0::15\\nTASH>>")
    elif command == "ifconfig eth0":
        sys.stdout.write("eth0\\n\\tinet: 10.0.2.15\\n\\tinet6: fec0::15\\nTASH>>")
    elif command.startswith("ping -c 3 10.0.2.2"):
        sys.stdout.write("TASH>>")
        sys.stdout.flush()
        sys.stdout.write("3 packets transmitted, ")
        sys.stdout.flush()
        sys.stdout.write("3 received, 0% packet loss\\n")
    elif command.startswith("ping6 -c 3 fec0::2"):
        sys.stdout.write("TASH>>")
        sys.stdout.flush()
        sys.stdout.write("3 packets transmitted, 3 received, 0% packet loss\\n")
    elif command == "netdb --host example.com":
        if mode == "dns-fail":
            sys.stdout.write("ERROR -- getaddrinfo failed.\\nTASH>>")
        else:
            sys.stdout.write("Host: example.com  IPv4 Addr: 93.184.216.34\\nTASH>>")
    elif command.startswith("ping -c 1 1.1.1.1"):
        received = 0 if mode == "public-loss" else 1
        sys.stdout.write("TASH>>")
        sys.stdout.flush()
        sys.stdout.write(f"1 packets transmitted, {{received}} received, 0% packet loss\\n")
        if not received:
            attempt += 1
    elif command == "net_stats":
        sys.stdout.write("TASH>>")
        sys.stdout.flush()
        sys.stdout.write("[driver] total recv 128\\t4\\n")
    elif command == "network_tc":
        fail = 1 if mode == "network-tc-fail" else 0
        sys.stdout.write("TASH>>")
        sys.stdout.flush()
        sys.stdout.write(f"########## Network TC End [PASS : 7, FAIL : {{fail}}] ##########\\n")
    elif command == "kernel_tc":
        fail = 1 if mode == "kernel-tc-fail" else 0
        sys.stdout.write(f"########## Kernel TC End [PASS : 11, FAIL : {{fail}}] ##########\\n")
        sys.stdout.flush()
        break
    else:
        raise AssertionError(command)
    sys.stdout.flush()
"""


class QemuArmv8mNetworkProtocolTest(RunnerHarness):
    def network_request(self, **changes):
        request = dataclasses.replace(self.request(), validate_network=True, timeout_sec=2.0)
        return dataclasses.replace(request, **changes)

    def test_fragmented_async_network_sequence_records_evidence(self) -> None:
        code = self.runner.run_kernel_tc(
            self.network_request(),
            self.child_command(network_child()),
        )
        result = self.read_result()

        self.assertEqual(0, code)
        self.assertEqual(11, result["pass_count"])
        self.assertEqual(0, result["fail_count"])
        self.assertEqual("pass", result["network"]["status"])
        self.assertIsNone(result["network"]["failure_reason"])
        self.assertEqual("10.0.2.15", result["network"]["ipv4"])
        self.assertTrue(result["network"]["dns_resolved"])
        self.assertEqual(3, result["network"]["pings"]["gateway-ping"]["received"])
        self.assertEqual(1, result["network"]["pings"]["public-ping"]["received"])
        self.assertEqual(7, result["network"]["network_tc_pass_count"])
        self.assertEqual(
            [
                "sleep 1",
                "help",
                "ifdown eth0",
                "ifup eth0",
                "ifconfig eth0 dhcp",
                "ifconfig eth0 fec0::15",
                "sleep 2",
                "ifconfig eth0",
                "ping -c 3 10.0.2.2",
                "ping6 -c 3 fec0::2",
                "netdb --host example.com",
                "ping -c 1 1.1.1.1",
                "net_stats",
                "network_tc",
                "sleep 2",
                "kernel_tc",
            ],
            result["network"]["commands"],
        )

    def test_public_ping_loss_is_recorded_but_not_fatal(self) -> None:
        code = self.runner.run_kernel_tc(
            self.network_request(),
            self.child_command(network_child("public-loss")),
        )
        result = self.read_result()

        self.assertEqual(0, code)
        self.assertEqual("pass", result["reason"])
        self.assertIsNone(result["network"]["failure_reason"])
        self.assertEqual(0, result["network"]["retry_count"])
        self.assertEqual(0, result["network"]["pings"]["public-ping"]["received"])

    def test_dns_failure_reinitializes_once_then_fails(self) -> None:
        code = self.runner.run_kernel_tc(
            self.network_request(),
            self.child_command(network_child("dns-fail")),
        )
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("network-dns", result["reason"])
        self.assertEqual(1, result["network"]["retry_count"])

    def test_dhcp_failure_reinitializes_once_then_fails(self) -> None:
        code = self.runner.run_kernel_tc(
            self.network_request(),
            self.child_command(network_child("dhcp-fail")),
        )
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("network-dhcp", result["reason"])
        self.assertEqual(1, result["network"]["retry_count"])

    def test_missing_help_registration_fails_before_interface_changes(self) -> None:
        script = network_child().replace("ifconfig ifdown", "ifconfig")
        code = self.runner.run_kernel_tc(
            self.network_request(),
            self.child_command(script),
        )
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("network-command-registration", result["reason"])
        self.assertEqual(["sleep 1", "help"], result["network"]["commands"])

    def test_network_tc_failure_is_distinct_from_kernel_tc(self) -> None:
        code = self.runner.run_kernel_tc(
            self.network_request(),
            self.child_command(network_child("network-tc-fail")),
        )
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("network-tc-fail", result["reason"])
        self.assertEqual(1, result["network"]["network_tc_fail_count"])
        self.assertIsNone(result["pass_count"])

    def test_kernel_tc_failure_preserves_kernel_counts(self) -> None:
        code = self.runner.run_kernel_tc(
            self.network_request(),
            self.child_command(network_child("kernel-tc-fail")),
        )
        result = self.read_result()

        self.assertEqual(1, code)
        self.assertEqual("kernel-tc-fail", result["reason"])
        self.assertEqual(11, result["pass_count"])
        self.assertEqual(1, result["fail_count"])

    def test_async_ping_without_statistics_times_out_in_that_phase(self) -> None:
        script = network_child().replace(
            'sys.stdout.write("3 packets transmitted, ")',
            'time.sleep(3)\n        sys.stdout.write("3 packets transmitted, ")',
            1,
        )
        code = self.runner.run_kernel_tc(
            self.network_request(timeout_sec=0.3),
            self.child_command(script),
        )

        self.assertEqual(1, code)
        self.assertEqual("network-ipv4-ping-timeout", self.read_result()["reason"])

    def test_expected_rejection_sends_no_network_commands(self) -> None:
        script = """
import select
import sys
sys.stdout.write("TASH>>QEMU_LOAD_REJECT common crc\\n")
sys.stdout.flush()
assert not select.select([sys.stdin], [], [], 0.1)[0]
"""
        request = dataclasses.replace(
            self.network_request(),
            expect_reject="QEMU_LOAD_REJECT common",
            forbid_marker="QEMU_APP1_STARTED",
        )

        code = self.runner.run_kernel_tc(request, self.child_command(script))
        result = self.read_result()

        self.assertEqual(0, code)
        self.assertEqual("expected-rejection", result["status"])
        self.assertIsNone(result["network"])


if __name__ == "__main__":
    unittest.main()
