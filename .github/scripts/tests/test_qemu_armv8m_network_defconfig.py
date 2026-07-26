from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RECIPES = ("hello", "loadable_all", "loadable_apps", "xip_all")

REQUIRED = {
    "CONFIG_NET=y",
    "CONFIG_NETDEVICES=y",
    "CONFIG_NET_LWIP=y",
    "CONFIG_NET_NETMGR=y",
    "CONFIG_NET_ETHERNET=y",
    "CONFIG_NET_IPv4=y",
    "CONFIG_NET_IPv6=y",
    "CONFIG_NET_TCP=y",
    "CONFIG_NET_UDP=y",
    "CONFIG_NET_RAW=y",
    "CONFIG_NET_LWIP_IGMP=y",
    "CONFIG_NET_IPv6_MLD=y",
    "CONFIG_NETUTILS_DHCPC=y",
    "CONFIG_NET_LWIP_NETDB=y",
    "CONFIG_NET_NETMON=y",
    "CONFIG_NET_STATS=y",
    "CONFIG_NET_CMDS=y",
    "CONFIG_NET_PING_CMD=y",
    "CONFIG_SYSTEM_NETDB=y",
    "CONFIG_EXAMPLES_TESTCASE_NETWORK=y",
}

NETWORK_TESTS = {
    "CONFIG_TC_NET_SOCKET=y",
    "CONFIG_TC_NET_SOCKET_SHARE=y",
    "CONFIG_TC_NET_PBUF=y",
    "CONFIG_TC_NET_SETSOCKOPT=y",
    "CONFIG_TC_NET_CONNECT=y",
    "CONFIG_TC_NET_CLOSE=y",
    "CONFIG_TC_NET_BIND=y",
    "CONFIG_TC_NET_LISTEN=y",
    "CONFIG_TC_NET_GETSOCKNAME=y",
    "CONFIG_TC_NET_GETSOCKOPT=y",
    "CONFIG_TC_NET_FCNTL=y",
    "CONFIG_TC_NET_IOCTL=y",
    "CONFIG_TC_NET_ACCEPT=y",
    "CONFIG_TC_NET_SEND=y",
    "CONFIG_TC_NET_RECV=y",
    "CONFIG_TC_NET_GETPEERNAME=y",
    "CONFIG_TC_NET_SENDTO=y",
    "CONFIG_TC_NET_RECVFROM=y",
    "CONFIG_TC_NET_SHUTDOWN=y",
    "CONFIG_TC_NET_DHCPC=y",
    "CONFIG_TC_NET_INET=y",
    "CONFIG_TC_NET_ETHER=y",
    "CONFIG_TC_NET_NETDB=y",
    "CONFIG_ITC_NET_CLOSE=y",
    "CONFIG_ITC_NET_LISTEN=y",
    "CONFIG_ITC_NET_SETSOCKOPT=y",
    "CONFIG_ITC_NET_SEND=y",
    "CONFIG_ITC_NET_INET=y",
    "CONFIG_ITC_NET_NETDB=y",
    "CONFIG_ITC_NET_CONNECT=y",
}

EXCLUDED = {
    "CONFIG_LWNL80211=y",
    "CONFIG_NETUTILS_DHCPD=y",
    "CONFIG_NETUTILS_TFTPC=y",
    "CONFIG_NETUTILS_WEBCLIENT=y",
    "CONFIG_SYSTEM_IPERF=y",
    "CONFIG_NET_SECURITY_TLS=y",
}


def enabled_lines(recipe: str) -> set[str]:
    path = ROOT / "build/configs/qemu-armv8m" / recipe / "defconfig"
    return {
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.startswith("CONFIG_") and "=" in line
    }


class QemuArmv8mNetworkDefconfigTest(unittest.TestCase):
    def test_all_recipes_enable_the_network_contract(self) -> None:
        for recipe in RECIPES:
            with self.subTest(recipe=recipe):
                lines = enabled_lines(recipe)
                self.assertFalse((REQUIRED | NETWORK_TESTS) - lines)

    def test_all_recipes_keep_out_of_scope_services_disabled(self) -> None:
        for recipe in RECIPES:
            with self.subTest(recipe=recipe):
                self.assertTrue(EXCLUDED.isdisjoint(enabled_lines(recipe)))

    def test_network_settings_are_identical_across_recipes(self) -> None:
        expected = enabled_lines(RECIPES[0]) & (REQUIRED | NETWORK_TESTS)
        for recipe in RECIPES[1:]:
            with self.subTest(recipe=recipe):
                self.assertEqual(expected, enabled_lines(recipe) & (REQUIRED | NETWORK_TESTS))

    def test_ethernet_netmgr_does_not_force_wifi_control_plane(self) -> None:
        kconfig = (ROOT / "os/net/netmgr/Kconfig").read_text(encoding="utf-8")
        self.assertNotIn("select LWNL80211", kconfig)

    def test_netmgr_registration_failure_releases_stack_state(self) -> None:
        source = (ROOT / "os/net/netmgr/netdev_mgr_internal.c").read_text(encoding="utf-8")
        self.assertIn("kmm_free(ops);", source)
        self.assertGreaterEqual(source.count("_nm_release_stack(dev);"), 2)


if __name__ == "__main__":
    unittest.main()
