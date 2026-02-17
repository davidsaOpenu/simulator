"""
Test Suite: NVMe Namespace Discovery
=====================================
Python 2.7 compatible. Uses subprocess.Popen instead of subprocess.run.

DoD: "Add new guest test that lists all namespaces."

Usage:
    sudo nosetests -v namespace_tests/test_namespace_discovery.py
"""

import os
import re
import subprocess
import stat
import unittest
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from device_map import (
    get_all_devices, get_sector_devices, get_object_devices,
    DEVICE_MAP, EXPECTED_DEVICE_COUNT, EXPECTED_CONTROLLER_COUNT,
    SECTOR, OBJECT
)

NVME_CLI_DIR = os.environ.get('NVME_CLI_DIR', '/home/esd/guest')


def run_cmd(args, cwd=None):
    """Run command, return (stdout_string, return_code). Python 2.7 safe."""
    proc = subprocess.Popen(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=cwd
    )
    stdout, stderr = proc.communicate()
    if isinstance(stdout, bytes):
        stdout = stdout.decode('utf-8', 'replace')
    return stdout, proc.returncode


def discover_nvme_block_devices():
    """Find all /dev/nvmeXnY block devices."""
    devices = []
    for entry in sorted(os.listdir('/dev')):
        if re.match(r'nvme\d+n\d+$', entry):
            devices.append('/dev/' + entry)
    return devices


def discover_nvme_controllers():
    """Find all /dev/nvmeX controller char devices."""
    controllers = []
    for entry in sorted(os.listdir('/dev')):
        if re.match(r'nvme\d+$', entry):
            controllers.append('/dev/' + entry)
    return controllers


class TestNamespaceDiscovery(unittest.TestCase):
    """Verify all NVMe namespaces from ssd.conf are visible in the guest."""

    def test_01_nvme_list_runs(self):
        """nvme list should execute successfully and show NVMe devices."""
        print("\n[TEST] Running nvme list...")
        output, rc = run_cmd(['./nvme', 'list'], cwd=NVME_CLI_DIR)
        print(output)
        self.assertEqual(rc, 0, "nvme list should succeed")
        self.assertIn('nvme', output.lower(),
                      "nvme list output should contain NVMe entries")

    def test_02_expected_block_devices_exist(self):
        """All expected /dev/nvmeXnY devices from device_map should exist."""
        print("\n[TEST] Checking expected block devices...")
        found = discover_nvme_block_devices()
        expected = get_all_devices()

        print("  Expected (%d devices): %s" % (len(expected), expected))
        print("  Found    (%d devices): %s" % (len(found), found))

        missing = [d for d in expected if d not in found]
        if missing:
            print("  MISSING: %s" % missing)

        for dev in expected:
            self.assertIn(dev, found,
                          "Expected device %s not found in /dev/" % dev)

    def test_03_correct_device_count(self):
        """Total NVMe block device count should match ssd.conf definition."""
        print("\n[TEST] Verifying device count...")
        found = discover_nvme_block_devices()
        print("  Expected: %d devices" % EXPECTED_DEVICE_COUNT)
        print("  Found:    %d devices" % len(found))
        self.assertGreaterEqual(len(found), EXPECTED_DEVICE_COUNT,
                                "Expected at least %d NVMe block devices" %
                                EXPECTED_DEVICE_COUNT)

    def test_04_correct_controller_count(self):
        """Number of NVMe controllers should match ssd.conf device count."""
        print("\n[TEST] Verifying controller count...")
        controllers = discover_nvme_controllers()
        print("  Expected: %d controllers" % EXPECTED_CONTROLLER_COUNT)
        print("  Found:    %d controllers: %s" % (len(controllers), controllers))
        self.assertEqual(len(controllers), EXPECTED_CONTROLLER_COUNT,
                         "Expected %d NVMe controllers" % EXPECTED_CONTROLLER_COUNT)

    def test_05_sector_devices_are_block_devices(self):
        """All sector-strategy devices should be valid block devices."""
        print("\n[TEST] Verifying sector devices are block devices...")
        sector_devs = get_sector_devices()
        for dev in sector_devs:
            if not os.path.exists(dev):
                self.skipTest("%s not found" % dev)
            mode = os.stat(dev).st_mode
            self.assertTrue(stat.S_ISBLK(mode),
                            "%s should be a block device" % dev)
            print("  %s -> block device OK" % dev)

    def test_06_devices_have_nonzero_size(self):
        """Each NVMe block device should have a non-zero size."""
        print("\n[TEST] Checking device sizes...")
        found = discover_nvme_block_devices()
        for dev in found:
            output, rc = run_cmd(['blockdev', '--getsize64', dev])
            if rc == 0:
                size = int(output.strip())
                print("  %s -> %d bytes (%.2f MB)" % (
                    dev, size, size / (1024.0 * 1024)))
                self.assertGreater(size, 0,
                                   "%s should have non-zero size" % dev)

    def test_07_mixed_strategies_present(self):
        """Verify both sector AND object namespaces are present."""
        print("\n[TEST] Verifying mixed strategies...")
        found = set(discover_nvme_block_devices())
        sector_devs = get_sector_devices()
        object_devs = get_object_devices()

        found_sector = [d for d in sector_devs if d in found]
        found_object = [d for d in object_devs if d in found]

        print("  Sector found: %d of %d" % (len(found_sector), len(sector_devs)))
        print("  Object found: %d of %d" % (len(found_object), len(object_devs)))

        self.assertGreater(len(found_sector), 0,
                           "Should have at least 1 sector namespace")
        self.assertGreater(len(found_object), 0,
                           "Should have at least 1 object namespace")

    def test_08_multi_namespace_controllers(self):
        """Verify controllers have the expected number of namespaces."""
        print("\n[TEST] Checking multi-namespace controllers...")
        found = discover_nvme_block_devices()

        # Group by controller
        ctrl_ns = {}
        for dev in found:
            m = re.match(r'/dev/nvme(\d+)n(\d+)', dev)
            if m:
                ctrl = int(m.group(1))
                ns = int(m.group(2))
                ctrl_ns.setdefault(ctrl, []).append(ns)

        for ctrl in sorted(ctrl_ns.keys()):
            ns_list = sorted(ctrl_ns[ctrl])
            print("  nvme%d: %d namespace(s) -> n%s" % (
                ctrl, len(ns_list),
                ",n".join(str(n) for n in ns_list)))

        # nvme4 should have 4 namespaces (nvme05 = 4x sector)
        if 4 in ctrl_ns:
            self.assertEqual(len(ctrl_ns[4]), 4,
                             "nvme4 (nvme05) should have 4 namespaces")

        # nvme5 should have 4 namespaces (nvme06 = 4x object)
        if 5 in ctrl_ns:
            self.assertEqual(len(ctrl_ns[5]), 4,
                             "nvme5 (nvme06) should have 4 namespaces")

    def test_09_print_full_device_map(self):
        """Print complete device map for documentation."""
        print("\n[TEST] Full device mapping:")
        print("  %-18s %-12s %-8s %-10s %s" % (
            "Guest Device", "Source", "NS", "Strategy", "Status"))
        print("  " + "-" * 60)
        for dev in get_all_devices():
            info = DEVICE_MAP[dev]
            strategy = "Sector" if info["strategy"] == SECTOR else "Object"
            exists = "EXISTS" if os.path.exists(dev) else "MISSING"
            print("  %-18s %-12s %-8s %-10s [%s]" % (
                dev, info["device"], info["ns"], strategy, exists))


if __name__ == '__main__':
    unittest.main()
