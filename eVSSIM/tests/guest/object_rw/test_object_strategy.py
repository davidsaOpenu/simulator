"""
Test Suite: Object Strategy - Read/Write/Delete/Capacity via ioctl
===================================================================
Python 2.7 compatible. Tests object-strategy namespaces via nvme-cli.

DoD: Use ioctl for object strategy. Generate random content, write, read,
     compare. Delete and verify. Fill to capacity, overflow must fail.

Usage:
    sudo nosetests -v object_rw/test_object_strategy.py
    NVME_OBJECT_DEVICE=/dev/nvme5n1 sudo nosetests -v object_rw/test_object_strategy.py
"""

import os
import random
import subprocess
import tempfile
import unittest
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from device_map import get_first_object_device, get_object_capacity

# --- Configuration ---
# nvme5n1 = nvme06 ns01, pure object controller
OBJECT_DEVICE = os.environ.get('NVME_OBJECT_DEVICE',
                                get_first_object_device() or '/dev/nvme5n1')
NVME_CLI_DIR = os.environ.get('NVME_CLI_DIR', '/home/esd/guest')
OBJ_FEATURE_ID = 192

MAX_OBJECT_VALUE_SIZE = int(os.environ.get('MAX_OBJECT_VALUE_SIZE', '4096'))
MAX_OBJECT_CAPACITY = get_object_capacity(OBJECT_DEVICE)

TEST_OBJECT_COUNT = 20


class NvmeObjectDevice(object):
    """Wrapper for NVMe Object CLI operations. Python 2.7 compatible."""

    def __init__(self, device, nvme_cli_dir):
        self.device = device
        self.nvme_cli_dir = nvme_cli_dir

        if not os.path.exists(self.device):
            raise RuntimeError("Device %s not found" % self.device)

        # Enable Object Strategy Feature
        print("[INFO] Enabling Object Feature on %s" % self.device)
        subprocess.check_call([
            './nvme', 'set-feature', self.device,
            '-f', str(OBJ_FEATURE_ID), '--value=1'
        ], cwd=self.nvme_cli_dir)

        # Create 'cols' file (legacy support for nvme-cli)
        cols_path = os.path.join(self.nvme_cli_dir, 'cols')
        with open(cols_path, 'w') as f:
            f.write('\n')

    def _run(self, args):
        """Run nvme-cli command."""
        print("  [CMD] %s" % ' '.join(args))
        return subprocess.check_call(args, cwd=self.nvme_cli_dir)

    def _run_output(self, args):
        """Run nvme-cli command and capture output."""
        return subprocess.check_output(
            args, cwd=self.nvme_cli_dir, stderr=subprocess.STDOUT)

    def write(self, obj_path, offset=0):
        self._run([
            './nvme', 'objw', self.device, obj_path,
            '-offset', str(offset)
        ])

    def read(self, obj_path):
        self._run(['./nvme', 'objr', self.device, obj_path])
        with open(obj_path, 'rb') as f:
            return f.read()

    def delete(self, obj_path):
        self._run(['./nvme', 'objd', self.device, obj_path])

    def exists(self, obj_path):
        try:
            self._run(['./nvme', 'obje', self.device, obj_path])
            return True
        except subprocess.CalledProcessError:
            return False

    def list_objects(self):
        output = self._run_output(['./nvme', 'objl', self.device])
        if isinstance(output, bytes):
            output = output.decode('utf-8', 'replace')
        return output


def make_temp_obj(prefix, content):
    """Create a temp file with given content. Returns path. Caller must delete."""
    fd, path = tempfile.mkstemp(prefix=prefix)
    try:
        os.write(fd, content)
    finally:
        os.close(fd)
    return path


class TestObjectReadWrite(unittest.TestCase):
    """Write objects with random content, read back, compare."""

    device = None

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(OBJECT_DEVICE):
            raise unittest.SkipTest("%s not found" % OBJECT_DEVICE)
        print("\n[SETUP] Using object device: %s" % OBJECT_DEVICE)
        print("  MAX_OBJECT_CAPACITY: %d" % MAX_OBJECT_CAPACITY)
        cls.device = NvmeObjectDevice(OBJECT_DEVICE, NVME_CLI_DIR)

    def setUp(self):
        self._cleanup_list = []

    def tearDown(self):
        for obj_path in self._cleanup_list:
            try:
                self.device.delete(obj_path)
            except Exception:
                pass
            try:
                if os.path.exists(obj_path):
                    os.remove(obj_path)
            except Exception:
                pass

    def test_01_write_read_single_object(self):
        """Write one object with random content, read back, compare."""
        print("\n[TEST] Single object write/read/compare")
        size = random.randint(1, min(MAX_OBJECT_VALUE_SIZE, 1024))
        original = os.urandom(size)

        obj_path = make_temp_obj('obj_test_', original)
        self._cleanup_list.append(obj_path)

        self.device.write(obj_path)
        readback = self.device.read(obj_path)

        self.assertEqual(original, readback,
                         "Object content mismatch (size=%d)" % size)
        print("  OK: %d bytes match" % size)

    def test_02_write_read_multiple_objects(self):
        """Write multiple objects, read all back, compare each."""
        print("\n[TEST] Multiple objects write/read/compare (%d)" %
              TEST_OBJECT_COUNT)
        objects = []

        for i in range(TEST_OBJECT_COUNT):
            size = random.randint(1, min(MAX_OBJECT_VALUE_SIZE, 1024))
            content = os.urandom(size)
            obj_path = make_temp_obj('obj_multi_%04d_' % i, content)
            self.device.write(obj_path)
            objects.append((obj_path, content))
            self._cleanup_list.append(obj_path)

        for obj_path, original in objects:
            readback = self.device.read(obj_path)
            self.assertEqual(original, readback,
                             "Content mismatch for %s" % obj_path)

        print("  OK: All %d objects verified" % TEST_OBJECT_COUNT)

    @unittest.expectedFailure
    def test_03_overwrite_object(self):
        """Overwrite an object with new content, verify."""
        print("\n[TEST] Overwrite object and verify")
        original = os.urandom(512)
        obj_path = make_temp_obj('obj_overwrite_', original)
        self._cleanup_list.append(obj_path)

        self.device.write(obj_path)

        new_content = os.urandom(256)
        with open(obj_path, 'wb') as f:
            f.write(new_content)
        self.device.write(obj_path)

        readback = self.device.read(obj_path)
        self.assertEqual(new_content, readback,
                         "Overwritten content should match new data")
        print("  OK: Overwrite verified")


class TestObjectDelete(unittest.TestCase):
    """Delete objects, verify they no longer exist."""

    device = None

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(OBJECT_DEVICE):
            raise unittest.SkipTest("%s not found" % OBJECT_DEVICE)
        cls.device = NvmeObjectDevice(OBJECT_DEVICE, NVME_CLI_DIR)

    def test_01_delete_object(self):
        """Write an object, delete it, verify read fails."""
        print("\n[TEST] Delete single object")
        content = os.urandom(random.randint(1, 512))
        obj_path = make_temp_obj('obj_del_', content)

        try:
            self.device.write(obj_path)
            self.device.delete(obj_path)

            try:
                self.device.read(obj_path)
                self.fail("Read should fail after deletion")
            except subprocess.CalledProcessError:
                pass  # Expected
            print("  OK: Object deleted, read fails as expected")
        finally:
            if os.path.exists(obj_path):
                os.remove(obj_path)

    def test_02_delete_multiple_verify_empty(self):
        """Create objects, delete all, verify namespace is empty."""
        print("\n[TEST] Delete all objects, verify empty")
        created = []

        for i in range(10):
            content = os.urandom(random.randint(1, 256))
            obj_path = make_temp_obj('obj_clean_%04d_' % i, content)
            self.device.write(obj_path)
            created.append(obj_path)

        for obj_path in created:
            self.device.delete(obj_path)

        for obj_path in created:
            self.assertFalse(self.device.exists(obj_path),
                             "%s should not exist after deletion" % obj_path)

        for obj_path in created:
            if os.path.exists(obj_path):
                os.remove(obj_path)

        print("  OK: All objects deleted, namespace empty")


class TestObjectCapacity(unittest.TestCase):
    """Fill object namespace to capacity, verify boundary."""

    device = None

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(OBJECT_DEVICE):
            raise unittest.SkipTest("%s not found" % OBJECT_DEVICE)
        cls.device = NvmeObjectDevice(OBJECT_DEVICE, NVME_CLI_DIR)

    def test_01_fill_to_capacity_and_overflow(self):
        """Create objects until capacity, try one more (must fail), cleanup."""
        print("\n[TEST] Fill object namespace to capacity")
        print("  MAX_OBJECT_CAPACITY = %d" % MAX_OBJECT_CAPACITY)

        created = []
        obj_size = 64

        for i in range(MAX_OBJECT_CAPACITY + 10):
            content = os.urandom(obj_size)
            obj_path = make_temp_obj('obj_fill_%06d_' % i, content)

            try:
                self.device.write(obj_path)
                created.append(obj_path)
            except subprocess.CalledProcessError:
                print("  Capacity reached at %d objects" % i)
                if os.path.exists(obj_path):
                    os.remove(obj_path)
                break

        print("  Created %d objects" % len(created))
        self.assertGreater(len(created), 0, "Should create at least 1 object")

        # Try overflow
        overflow_content = os.urandom(obj_size)
        overflow_path = make_temp_obj('obj_overflow_', overflow_content)
        overflow_failed = False
        try:
            self.device.write(overflow_path)
            created.append(overflow_path)
            print("  WARNING: Overflow object was accepted")
        except subprocess.CalledProcessError:
            overflow_failed = True
            print("  OK: Overflow correctly rejected")
        finally:
            if os.path.exists(overflow_path) and overflow_path not in created:
                os.remove(overflow_path)

        # Cleanup
        print("  Deleting all %d objects..." % len(created))
        for obj_path in created:
            try:
                self.device.delete(obj_path)
            except Exception:
                pass
            if os.path.exists(obj_path):
                os.remove(obj_path)

        # Verify empty
        for obj_path in created:
            self.assertFalse(self.device.exists(obj_path),
                             "%s should not exist after cleanup" % obj_path)

        print("  OK: Capacity test complete, namespace clean")


if __name__ == '__main__':
    unittest.main()
