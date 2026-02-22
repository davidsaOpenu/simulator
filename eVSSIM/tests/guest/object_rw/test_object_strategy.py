"""
Test Suite: Object Strategy - Basic Object CRUD via ioctl
=========================================================
Tests object-strategy namespaces via nvme-cli.

Basic operations: create (write), read, update (overwrite), delete.
Also validates that object operations fail on sector-based namespaces.

Usage:
    sudo nosetests -v object_rw/test_object_strategy.py
    NVME_OBJECT_DEVICE=/dev/nvme3n1 sudo nosetests -v object_rw/test_object_strategy.py
"""

import os
import random
import subprocess
import tempfile
import unittest
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from device_map import get_first_object_device, get_first_sector_device

# --- Configuration ---
OBJECT_DEVICE = os.environ.get('NVME_OBJECT_DEVICE', get_first_object_device())
SECTOR_DEVICE = os.environ.get('NVME_SECTOR_DEVICE', get_first_sector_device())
NVME_CLI_DIR = os.environ.get('NVME_CLI_DIR', '/home/esd/guest')
OBJ_FEATURE_ID = 192

MAX_OBJECT_VALUE_SIZE = int(os.environ.get('MAX_OBJECT_VALUE_SIZE', '4096'))


class NvmeObjectDevice(object):
    """Wrapper for NVMe Object CLI operations."""

    def __init__(self, device, nvme_cli_dir):
        self.device = device
        self.nvme_cli_dir = nvme_cli_dir

        if not os.path.exists(self.device):
            raise RuntimeError("Device %s not found" % self.device)

        subprocess.check_call([
            './nvme', 'set-feature', self.device,
            '-f', str(OBJ_FEATURE_ID), '--value=1'
        ], cwd=self.nvme_cli_dir)

        cols_path = os.path.join(self.nvme_cli_dir, 'cols')
        with open(cols_path, 'w') as f:
            f.write('\n')

    def write(self, obj_path):
        subprocess.check_call(
            ['./nvme', 'objw', self.device, obj_path, '-offset', '0'],
            cwd=self.nvme_cli_dir)

    def read(self, obj_path):
        subprocess.check_call(
            ['./nvme', 'objr', self.device, obj_path],
            cwd=self.nvme_cli_dir)
        with open(obj_path, 'rb') as f:
            return f.read()

    def delete(self, obj_path):
        subprocess.check_call(
            ['./nvme', 'objd', self.device, obj_path],
            cwd=self.nvme_cli_dir)


def make_temp_obj(content):
    """Create a temp file with given content. Returns path. Caller must delete."""
    fd, path = tempfile.mkstemp(prefix='nvme_obj_')
    try:
        os.write(fd, content)
    finally:
        os.close(fd)
    return path


class TestObjectCRUD(unittest.TestCase):
    """Basic object CRUD on an object-strategy namespace."""

    device = None

    @classmethod
    def setUpClass(cls):
        if not OBJECT_DEVICE or not os.path.exists(OBJECT_DEVICE):
            raise unittest.SkipTest("%s not found" % OBJECT_DEVICE)
        print("\n[SETUP] Using object device: %s" % OBJECT_DEVICE)
        cls.device = NvmeObjectDevice(OBJECT_DEVICE, NVME_CLI_DIR)

    def setUp(self):
        self._cleanup = []

    def tearDown(self):
        for path in self._cleanup:
            try:
                self.device.delete(path)
            except Exception:
                pass
            try:
                os.remove(path)
            except Exception:
                pass

    def test_01_create_and_read(self):
        """Write one object with random content, read back, compare."""
        content = os.urandom(random.randint(1, min(MAX_OBJECT_VALUE_SIZE, 1024)))
        path = make_temp_obj(content)
        self._cleanup.append(path)

        self.device.write(path)
        readback = self.device.read(path)

        self.assertEqual(content, readback,
                         "Object content mismatch")

    def test_02_update(self):
        """Overwrite an existing object with new content, read back, compare."""
        original = os.urandom(512)
        path = make_temp_obj(original)
        self._cleanup.append(path)

        self.device.write(path)

        new_content = os.urandom(256)
        with open(path, 'wb') as f:
            f.write(new_content)
        self.device.write(path)

        readback = self.device.read(path)
        self.assertEqual(new_content, readback,
                         "Overwritten content should match new data")

    def test_03_delete(self):
        """Write an object, delete it, verify a subsequent read fails."""
        content = os.urandom(random.randint(1, 512))
        path = make_temp_obj(content)
        self._cleanup.append(path)

        self.device.write(path)
        self.device.delete(path)

        try:
            self.device.read(path)
            self.fail("Read after delete should have failed")
        except subprocess.CalledProcessError:
            pass  # Expected: object no longer exists


class TestObjectOpsOnSectorNamespace(unittest.TestCase):
    """Validate that object operations fail on sector-based namespaces."""

    def test_object_write_fails_on_sector_ns(self):
        """objw on a sector namespace should fail (no object feature enabled)."""
        if not SECTOR_DEVICE or not os.path.exists(SECTOR_DEVICE):
            raise unittest.SkipTest("%s not found" % SECTOR_DEVICE)

        content = os.urandom(64)
        path = make_temp_obj(content)
        try:
            rc = subprocess.call(
                ['./nvme', 'objw', SECTOR_DEVICE, path, '-offset', '0'],
                cwd=NVME_CLI_DIR
            )
            self.assertNotEqual(rc, 0,
                "objw on sector namespace %s should fail" % SECTOR_DEVICE)
        finally:
            os.remove(path)


if __name__ == '__main__':
    unittest.main()
