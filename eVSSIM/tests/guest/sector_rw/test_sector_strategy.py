"""
Test Suite: Sector Strategy - Basic File CRUD
=============================================
Tests sector-strategy namespaces via filesystem mount.

Basic operations: create (write), read, update (overwrite), delete.
A single file is used per operation.

Usage:
    sudo nosetests -v sector_rw/test_sector_strategy.py
    NVME_SECTOR_DEVICE=/dev/nvme1n1 sudo nosetests -v sector_rw/test_sector_strategy.py
"""

import os
import hashlib
import random
import subprocess
import unittest
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from device_map import get_first_sector_device

# --- Configuration ---
SECTOR_DEVICE = os.environ.get('NVME_SECTOR_DEVICE', get_first_sector_device())
MOUNT_POINT = '/tmp/nvme_sector_test_mount'
FS_TYPE = 'ext2'
MAX_FILE_SIZE = 8192


def run_cmd(args, check=True):
    """Run command. Python 2.7 compatible."""
    print("  [CMD] %s" % ' '.join(args))
    try:
        return subprocess.check_output(args, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        if check:
            raise
        return e.output


def md5(data):
    return hashlib.md5(data).hexdigest()


def is_mounted(path):
    with open('/proc/mounts', 'r') as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2 and parts[1] == path:
                return True
    return False


def ensure_unmounted(path):
    if is_mounted(path):
        subprocess.call(['sync'])
        subprocess.call(['umount', path])


def _clean_mount():
    """Remove all files from the mount point without reformatting."""
    if not os.path.ismount(MOUNT_POINT):
        return
    for item in os.listdir(MOUNT_POINT):
        if item == 'lost+found':
            continue
        path = os.path.join(MOUNT_POINT, item)
        if os.path.isfile(path):
            os.remove(path)
    subprocess.call(['sync'])


def setUpModule():
    """Format and mount the sector device once for all test classes."""
    if not SECTOR_DEVICE or not os.path.exists(SECTOR_DEVICE):
        raise unittest.SkipTest("%s not found" % SECTOR_DEVICE)

    print("\n[SETUP] Using sector device: %s" % SECTOR_DEVICE)
    ensure_unmounted(MOUNT_POINT)
    if not os.path.exists(MOUNT_POINT):
        os.makedirs(MOUNT_POINT)

    print("[SETUP] Formatting %s as %s..." % (SECTOR_DEVICE, FS_TYPE))
    run_cmd(['mkfs.%s' % FS_TYPE, '-F', SECTOR_DEVICE])

    print("[SETUP] Mounting at %s..." % MOUNT_POINT)
    run_cmd(['mount', SECTOR_DEVICE, MOUNT_POINT])


def tearDownModule():
    """Unmount the sector device after all test classes."""
    ensure_unmounted(MOUNT_POINT)


class TestSectorCRUD(unittest.TestCase):
    """Basic file CRUD on a sector-strategy namespace."""

    def setUp(self):
        _clean_mount()

    def tearDown(self):
        _clean_mount()

    def _write_file(self, filepath, content):
        with open(filepath, 'wb') as f:
            f.write(content)
        subprocess.call(['sync'])

    def _read_file(self, filepath):
        try:
            with open('/proc/sys/vm/drop_caches', 'w') as f:
                f.write('3')
        except (IOError, OSError):
            pass
        with open(filepath, 'rb') as f:
            return f.read()

    def test_01_create_and_read(self):
        """Write a single file with random content, read back, compare."""
        filepath = os.path.join(MOUNT_POINT, 'test.bin')
        size = random.randint(1, MAX_FILE_SIZE)
        content = os.urandom(size)

        self._write_file(filepath, content)
        readback = self._read_file(filepath)

        self.assertEqual(md5(content), md5(readback),
                         "Content mismatch for %d bytes" % size)

    def test_02_update(self):
        """Overwrite a file with new content, verify new content persists."""
        filepath = os.path.join(MOUNT_POINT, 'test_update.bin')

        self._write_file(filepath, os.urandom(4096))
        new_content = os.urandom(2048)
        self._write_file(filepath, new_content)
        readback = self._read_file(filepath)

        self.assertEqual(md5(new_content), md5(readback),
                         "Readback should match overwritten content")

    def test_03_delete(self):
        """Write a file, delete it, verify it no longer exists."""
        filepath = os.path.join(MOUNT_POINT, 'test_delete.bin')
        self._write_file(filepath, os.urandom(512))

        self.assertTrue(os.path.exists(filepath))
        os.remove(filepath)
        subprocess.call(['sync'])

        self.assertFalse(os.path.exists(filepath),
                         "%s should not exist after deletion" % filepath)


if __name__ == '__main__':
    unittest.main()
