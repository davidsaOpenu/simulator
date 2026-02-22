"""
Test Suite: NVMe Namespace Discovery
=====================================
Parses ssd.conf and verifies all expected namespaces are visible in the guest.

Usage:
    sudo nosetests -v namespace_tests/test_namespace_discovery.py
"""

import os
import re
import stat
import subprocess
import unittest
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from device_map import get_all_devices, get_sector_devices, get_object_devices

NVME_CLI_DIR = os.environ.get('NVME_CLI_DIR', '/home/esd/guest')


def discover_nvme_block_devices():
    """Find all /dev/nvmeXnY block devices present in the guest."""
    devices = []
    for entry in sorted(os.listdir('/dev')):
        if re.match(r'nvme\d+n\d+$', entry):
            devices.append('/dev/' + entry)
    return devices


class TestNamespaceDiscovery(unittest.TestCase):
    """Verify all NVMe namespaces from ssd.conf are visible in the guest."""

    def test_01_nvme_list_runs(self):
        """nvme list should execute successfully and report NVMe devices."""
        proc = subprocess.Popen(
            ['./nvme', 'list'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=NVME_CLI_DIR
        )
        stdout, _ = proc.communicate()
        if isinstance(stdout, bytes):
            stdout = stdout.decode('utf-8', 'replace')
        self.assertEqual(proc.returncode, 0, "nvme list should succeed")
        self.assertIn('nvme', stdout.lower(),
                      "nvme list output should mention NVMe devices")

    def test_02_expected_block_devices_exist(self):
        """All /dev/nvmeXnY paths derived from ssd.conf should be present."""
        found = set(discover_nvme_block_devices())
        expected = get_all_devices()
        missing = [d for d in expected if d not in found]
        self.assertEqual(missing, [],
                         "Devices from ssd.conf missing in guest: %s" % missing)

    def test_03_sector_devices_are_block_devices(self):
        """All sector-strategy devices should be valid block devices."""
        errors = []
        for dev in get_sector_devices():
            if not os.path.exists(dev):
                errors.append("%s missing" % dev)
            elif not stat.S_ISBLK(os.stat(dev).st_mode):
                errors.append("%s is not a block device" % dev)
        self.assertEqual(errors, [], "; ".join(errors))

    def test_04_mixed_strategies_present(self):
        """Both sector and object namespaces should be present per ssd.conf."""
        self.assertGreater(len(get_sector_devices()), 0,
                           "No sector namespaces found in ssd.conf")
        self.assertGreater(len(get_object_devices()), 0,
                           "No object namespaces found in ssd.conf")


if __name__ == '__main__':
    unittest.main()
