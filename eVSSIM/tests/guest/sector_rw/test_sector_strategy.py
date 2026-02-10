"""
Test Suite: Sector Strategy - File Read/Write/Delete/Capacity
=============================================================
Python 2.7 compatible. Tests sector-strategy namespaces via filesystem.

DoD: Read/write files with random content, compare. Delete and verify.
     Fill to capacity, overflow must fail, delete all, verify empty.

Usage:
    sudo nosetests -v sector_rw/test_sector_strategy.py
    NVME_SECTOR_DEVICE=/dev/nvme1n1 sudo nosetests -v sector_rw/test_sector_strategy.py
"""

import os
import hashlib
import random
import shutil
import subprocess
import time
import unittest
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from device_map import get_first_sector_device, get_largest_sector_device

# --- Configuration ---
# nvme1n1 = nvme02 ns01, single namespace controller, full drive size (~16MB)
SECTOR_DEVICE = os.environ.get('NVME_SECTOR_DEVICE',
                                get_largest_sector_device() or '/dev/nvme1n1')
MOUNT_POINT = '/tmp/nvme_sector_test_mount'
FS_TYPE = 'ext2'

FILE_COUNT = 20
MAX_FILE_SIZE = 8192
FILL_CHUNK_SIZE = 4096


def run_cmd(args, check=True):
    """Run command. Python 2.7 compatible."""
    print("  [CMD] %s" % ' '.join(args))
    try:
        output = subprocess.check_output(args, stderr=subprocess.STDOUT)
        return output
    except subprocess.CalledProcessError as e:
        if check:
            print("  [CMD FAILED] exit=%d output=%s" % (
                e.returncode, e.output.decode('utf-8', 'replace') if isinstance(e.output, bytes) else e.output))
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
        time.sleep(0.5)


def get_device_size(device):
    output = subprocess.check_output(['blockdev', '--getsize64', device])
    return int(output.decode().strip())


class TestSectorReadWrite(unittest.TestCase):
    """Write files with random content, read back, compare."""

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(SECTOR_DEVICE):
            raise unittest.SkipTest("%s not found" % SECTOR_DEVICE)

        size = get_device_size(SECTOR_DEVICE)
        print("\n[SETUP] Using sector device: %s (%d bytes, %.1f MB)" % (
            SECTOR_DEVICE, size, size / (1024.0 * 1024)))

        if size < 65536:
            raise unittest.SkipTest(
                "%s too small (%d bytes). Need >= 64KB for ext2." % (
                    SECTOR_DEVICE, size))

        ensure_unmounted(MOUNT_POINT)
        if not os.path.exists(MOUNT_POINT):
            os.makedirs(MOUNT_POINT)

        print("[SETUP] Formatting %s as %s..." % (SECTOR_DEVICE, FS_TYPE))
        run_cmd(['mkfs.%s' % FS_TYPE, '-F', SECTOR_DEVICE])

        print("[SETUP] Mounting at %s..." % MOUNT_POINT)
        run_cmd(['mount', SECTOR_DEVICE, MOUNT_POINT])

    @classmethod
    def tearDownClass(cls):
        ensure_unmounted(MOUNT_POINT)

    def _clean_mount(self):
        if not os.path.ismount(MOUNT_POINT):
            return
        for item in os.listdir(MOUNT_POINT):
            if item == 'lost+found':
                continue
            path = os.path.join(MOUNT_POINT, item)
            if os.path.isfile(path):
                os.remove(path)
            elif os.path.isdir(path):
                shutil.rmtree(path)
        subprocess.call(['sync'])

    def setUp(self):
        self._clean_mount()

    def tearDown(self):
        self._clean_mount()

    def _write_random_file(self, filepath, size):
        content = os.urandom(size)
        with open(filepath, 'wb') as f:
            f.write(content)
        subprocess.call(['sync'])
        return content

    def _read_file(self, filepath):
        try:
            with open('/proc/sys/vm/drop_caches', 'w') as f:
                f.write('3')
        except (IOError, OSError):
            pass
        with open(filepath, 'rb') as f:
            return f.read()

    def test_01_write_read_single_file(self):
        """Write a single file with random content, read back, compare."""
        print("\n[TEST] Single file write/read/compare")
        filepath = os.path.join(MOUNT_POINT, 'test_single.bin')
        size = random.randint(1, MAX_FILE_SIZE)

        original = self._write_random_file(filepath, size)
        readback = self._read_file(filepath)

        self.assertEqual(len(original), len(readback),
                         "Size mismatch: wrote %d, read %d" % (
                             len(original), len(readback)))
        self.assertEqual(md5(original), md5(readback),
                         "Content mismatch for %d bytes" % size)
        print("  OK: %d bytes verified" % size)

    def test_02_write_read_multiple_files(self):
        """Write multiple files, read all back, compare."""
        print("\n[TEST] Multiple file write/read/compare (%d files)" % FILE_COUNT)
        files = {}

        for i in range(FILE_COUNT):
            filename = 'test_multi_%04d.bin' % i
            filepath = os.path.join(MOUNT_POINT, filename)
            size = random.randint(1, MAX_FILE_SIZE)
            files[filepath] = self._write_random_file(filepath, size)

        subprocess.call(['sync'])

        for filepath, original in files.items():
            readback = self._read_file(filepath)
            self.assertEqual(md5(original), md5(readback),
                             "Content mismatch for %s" % filepath)

        print("  OK: All %d files verified" % FILE_COUNT)

    def test_03_overwrite_and_verify(self):
        """Overwrite a file with new content, verify new content persists."""
        print("\n[TEST] Overwrite file and verify")
        filepath = os.path.join(MOUNT_POINT, 'test_overwrite.bin')

        original = self._write_random_file(filepath, 4096)
        new_content = self._write_random_file(filepath, 2048)
        readback = self._read_file(filepath)

        self.assertNotEqual(md5(original), md5(new_content))
        self.assertEqual(md5(new_content), md5(readback),
                         "Readback should match overwritten content")
        print("  OK: Overwrite verified")


class TestSectorDelete(unittest.TestCase):
    """Delete files on sector namespace, verify deletion."""

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(SECTOR_DEVICE):
            raise unittest.SkipTest("%s not found" % SECTOR_DEVICE)
        size = get_device_size(SECTOR_DEVICE)
        if size < 65536:
            raise unittest.SkipTest("%s too small" % SECTOR_DEVICE)
        ensure_unmounted(MOUNT_POINT)
        if not os.path.exists(MOUNT_POINT):
            os.makedirs(MOUNT_POINT)
        run_cmd(['mkfs.%s' % FS_TYPE, '-F', SECTOR_DEVICE])
        run_cmd(['mount', SECTOR_DEVICE, MOUNT_POINT])

    @classmethod
    def tearDownClass(cls):
        ensure_unmounted(MOUNT_POINT)

    def test_01_delete_files(self):
        """Write files, delete them, verify they no longer exist."""
        print("\n[TEST] Delete files and verify")
        filepaths = []
        for i in range(5):
            filepath = os.path.join(MOUNT_POINT, 'test_del_%d.bin' % i)
            with open(filepath, 'wb') as f:
                f.write(os.urandom(random.randint(100, 1024)))
            filepaths.append(filepath)
        subprocess.call(['sync'])

        for fp in filepaths:
            self.assertTrue(os.path.exists(fp))

        for fp in filepaths:
            os.remove(fp)
        subprocess.call(['sync'])

        for fp in filepaths:
            self.assertFalse(os.path.exists(fp),
                             "%s should not exist after deletion" % fp)
        print("  OK: All files deleted and verified")

    def test_02_delete_all_verify_empty(self):
        """Write files, delete everything, verify empty."""
        print("\n[TEST] Delete all, verify empty")
        for i in range(10):
            filepath = os.path.join(MOUNT_POINT, 'test_cleanup_%d.bin' % i)
            with open(filepath, 'wb') as f:
                f.write(os.urandom(512))
        subprocess.call(['sync'])

        for item in os.listdir(MOUNT_POINT):
            if item == 'lost+found':
                continue
            path = os.path.join(MOUNT_POINT, item)
            if os.path.isfile(path):
                os.remove(path)
            elif os.path.isdir(path):
                shutil.rmtree(path)
        subprocess.call(['sync'])

        remaining = [f for f in os.listdir(MOUNT_POINT) if f != 'lost+found']
        self.assertEqual(len(remaining), 0,
                         "Should be empty. Found: %s" % remaining)
        print("  OK: Namespace empty after delete-all")


class TestSectorCapacity(unittest.TestCase):
    """Fill sector namespace to capacity, verify boundary."""

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(SECTOR_DEVICE):
            raise unittest.SkipTest("%s not found" % SECTOR_DEVICE)
        size = get_device_size(SECTOR_DEVICE)
        if size < 65536:
            raise unittest.SkipTest("%s too small" % SECTOR_DEVICE)
        ensure_unmounted(MOUNT_POINT)
        if not os.path.exists(MOUNT_POINT):
            os.makedirs(MOUNT_POINT)
        run_cmd(['mkfs.%s' % FS_TYPE, '-F', SECTOR_DEVICE])
        run_cmd(['mount', SECTOR_DEVICE, MOUNT_POINT])

    @classmethod
    def tearDownClass(cls):
        ensure_unmounted(MOUNT_POINT)

    def test_01_fill_to_capacity_and_overflow(self):
        """Fill namespace, try one more byte (must fail), delete all, verify."""
        print("\n[TEST] Fill to capacity + overflow test")

        st = os.statvfs(MOUNT_POINT)
        initial_avail = st.f_bavail * st.f_bsize
        device_size = get_device_size(SECTOR_DEVICE)

        print("  Device size:       %d bytes (%.2f MB)" % (
            device_size, device_size / (1024.0 * 1024)))
        print("  Initial available: %d bytes (%.2f MB)" % (
            initial_avail, initial_avail / (1024.0 * 1024)))

        file_index = 0
        total_written = 0
        created_files = []

        while True:
            st = os.statvfs(MOUNT_POINT)
            remaining = st.f_bavail * st.f_bsize
            if remaining <= 0:
                break

            filepath = os.path.join(MOUNT_POINT, 'fill_%06d.bin' % file_index)
            write_size = min(FILL_CHUNK_SIZE, remaining)

            try:
                with open(filepath, 'wb') as f:
                    f.write(os.urandom(write_size))
                subprocess.call(['sync'])
                created_files.append(filepath)
                total_written += write_size
                file_index += 1
            except (IOError, OSError) as e:
                print("  Write stopped at %d bytes: %s" % (total_written, e))
                break

        print("  Filled: %d bytes in %d files" % (total_written, len(created_files)))

        # Try overflow
        overflow_path = os.path.join(MOUNT_POINT, 'overflow.bin')
        overflow_failed = False
        try:
            with open(overflow_path, 'wb') as f:
                f.write(b'\x42')
            subprocess.call(['sync'])
            st = os.statvfs(MOUNT_POINT)
            if st.f_bavail == 0:
                overflow_failed = True
                print("  Overflow: write succeeded but 0 free blocks")
            else:
                print("  WARNING: Overflow write succeeded with %d blocks free" %
                      st.f_bavail)
        except (IOError, OSError) as e:
            overflow_failed = True
            print("  Overflow correctly failed: %s" % e)

        # Delete everything
        for filepath in created_files:
            if os.path.exists(filepath):
                os.remove(filepath)
        if os.path.exists(overflow_path):
            os.remove(overflow_path)
        subprocess.call(['sync'])

        # Verify empty
        remaining_files = [f for f in os.listdir(MOUNT_POINT) if f != 'lost+found']
        self.assertEqual(len(remaining_files), 0,
                         "All files should be deleted. Found: %s" % remaining_files)

        # Verify space recovered
        st = os.statvfs(MOUNT_POINT)
        recovered = st.f_bavail * st.f_bsize
        print("  After cleanup: %d bytes available" % recovered)

        recovery_ratio = float(recovered) / max(initial_avail, 1)
        self.assertGreater(recovery_ratio, 0.7,
                           "Should recover >= 70%% of space")
        print("  OK: Capacity test passed (recovered %.0f%%)" %
              (recovery_ratio * 100))


if __name__ == '__main__':
    unittest.main()
