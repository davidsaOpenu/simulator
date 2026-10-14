#!/usr/bin/env python
"""
exofs bring-up tests: kernel, NVMe object mode, module, mkfs.exofs and mount.

These follow the bring-up order of the test plan and check every step
explicitly, so a failure here points directly at the broken stage:

  3. nvme-cli: feature 192 (key/value command set) on the drive
  4. insmod exofs.ko, filesystem registered
  5. mkfs.exofs + mount -t exofs, superblock sanity
"""

from __future__ import print_function

import errno
import glob
import os
import platform
import unittest

import exofs_helpers as h


def setup_module():
    assert os.geteuid() == 0, "exofs tests must run as root"
    h.wait_for_device(h.DEVICE)


def teardown_module():
    h.tear_down()


class TestKernelAndDrives(unittest.TestCase):

    def test_kernel_version(self):
        expected = os.environ.get("EXOFS_EXPECTED_KERNEL", "7.0.2")
        self.assertTrue(platform.release().startswith(expected),
                        "guest runs kernel %s, expected %s" % (platform.release(), expected))

    def test_main_drive_is_block_device(self):
        import stat
        h.wait_for_device(h.DEVICE)
        self.assertTrue(stat.S_ISBLK(os.stat(h.DEVICE).st_mode), "%s is not a block device" % h.DEVICE)

    def test_all_provisioned_drives_present(self):
        # every drive qemu was started with must show up in the guest
        raw = os.environ.get("EVSSIM_PROVISIONED_DEVICE_COUNT", "").strip()
        if not raw.isdigit() or int(raw) <= 0:
            raise unittest.SkipTest("EVSSIM_PROVISIONED_DEVICE_COUNT is not set")
        expected = int(raw)
        h.wait_for(lambda: len(glob.glob("/dev/nvme*n1")) >= expected, timeout=h.DEVICE_WAIT_SECONDS)
        found = sorted(glob.glob("/dev/nvme*n1"))
        self.assertEqual(len(found), expected, "expected %d NVMe drive(s), found %d: %s"
                         % (expected, len(found), ", ".join(found) or "nothing"))

    def test_exofs_module_file_exists(self):
        self.assertTrue(os.path.isfile(h.KO_PATH), "missing %s" % h.KO_PATH)

    def test_tools_present(self):
        h.run([h.NVME, "version"])
        rc, out = h.run([h.MKFS], check=False)
        self.assertNotEqual(rc, 0, "mkfs.exofs without arguments must fail")
        self.assertIn("usage", out.lower())


class TestStep3NvmeObjectMode(unittest.TestCase):

    def test_enable_object_mode(self):
        h.nvme_set_object_mode(h.DEVICE, True)
        value = h.nvme_get_object_mode(h.DEVICE)
        if value is not None:
            self.assertEqual(value & 1, 1, "feature %d reads back %r after enabling"
                             % (h.OBJ_FEATURE_ID, value))

    def test_enable_object_mode_spare(self):
        if not os.path.exists(h.SPARE_DEVICE):
            raise unittest.SkipTest("no spare drive %s" % h.SPARE_DEVICE)
        h.nvme_set_object_mode(h.SPARE_DEVICE, True)


class TestStep4Module(unittest.TestCase):

    def test_a_insmod(self):
        h.insmod()
        self.assertTrue(h.module_loaded())
        self.assertTrue(h.fs_registered())

    def test_b_insmod_twice_fails_cleanly(self):
        h.insmod()
        rc, out = h.run(["insmod", h.KO_PATH], check=False)
        self.assertNotEqual(rc, 0, "second insmod must fail (module already loaded)")
        self.assertTrue(h.module_loaded())

    def test_c_modinfo(self):
        _, out = h.run(["modinfo", h.KO_PATH])
        self.assertIn("GPL", out)
        self.assertIn("exofs", out.lower())

    def test_d_rmmod_and_reload(self):
        h.insmod()
        h.tear_down()
        h.rmmod()
        self.assertFalse(h.module_loaded(), "exofs still loaded after rmmod")
        self.assertFalse(h.fs_registered(), "exofs still registered after rmmod")
        h.insmod()


class TestStep5FormatAndMount(unittest.TestCase):

    def setUp(self):
        h.enable_object_mode_everywhere()
        h.insmod()
        h.tear_down()

    def tearDown(self):
        h.tear_down()

    def test_mkfs_bad_arguments(self):
        rc, _ = h.run([h.MKFS, "/dev/does-not-exist"], check=False)
        self.assertNotEqual(rc, 0, "mkfs.exofs on a missing device must fail")
        rc, _ = h.run([h.MKFS, h.DEVICE, "extra-arg"], check=False)
        self.assertNotEqual(rc, 0, "mkfs.exofs with extra arguments must fail")

    def test_mkfs_and_mount(self):
        h.mkfs(h.DEVICE)
        h.mount(h.DEVICE, h.MOUNTPOINT)
        entry = h.mount_entry(h.MOUNTPOINT)
        self.assertIsNotNone(entry)
        self.assertEqual(entry[2], "exofs")
        self.assertEqual(os.path.realpath(entry[0]), os.path.realpath(h.DEVICE))

    def test_superblock_magic(self):
        h.mkfs(h.DEVICE)
        h.mount()
        self.assertEqual(h.fs_magic(), h.EXOFS_SUPER_MAGIC)

    def test_fresh_fs_root_is_empty(self):
        h.mkfs(h.DEVICE)
        h.mount()
        self.assertEqual(os.listdir(h.MOUNTPOINT), [])
        st = os.stat(h.MOUNTPOINT)
        import stat
        self.assertTrue(stat.S_ISDIR(st.st_mode))
        self.assertEqual(st.st_ino, 0x10001, "root inode must be EXOFS_ROOT_ID")
        self.assertEqual(os.stat(os.path.join(h.MOUNTPOINT, ".")).st_ino, st.st_ino)
        self.assertEqual(os.stat(os.path.join(h.MOUNTPOINT, "..")).st_ino,
                         os.stat(os.path.dirname(h.MOUNTPOINT)).st_ino)

    def test_fresh_fs_file_count(self):
        h.mkfs(h.DEVICE)
        h.mount()
        self.assertEqual(h.statvfs_files(), 1, "a fresh exofs holds exactly the root inode")

    def test_mkfs_wipes_previous_tree(self):
        h.mkfs(h.DEVICE)
        h.mount()
        os.mkdir(os.path.join(h.MOUNTPOINT, "old-dir"))
        h.write_file(os.path.join(h.MOUNTPOINT, "old-file"), b"old")
        h.umount()
        h.mkfs(h.DEVICE)
        h.mount()
        self.assertEqual(os.listdir(h.MOUNTPOINT), [], "mkfs must start from an empty root")

    def test_mount_unmount_cycles(self):
        h.mkfs(h.DEVICE)
        for i in range(5):
            h.mount()
            h.write_file(os.path.join(h.MOUNTPOINT, "cycle-%d" % i), b"x" * (i + 1))
            h.umount()
        h.mount()
        self.assertEqual(sorted(os.listdir(h.MOUNTPOINT)), ["cycle-%d" % i for i in range(5)])
        for i in range(5):
            self.assertEqual(h.read_file(os.path.join(h.MOUNTPOINT, "cycle-%d" % i)), b"x" * (i + 1))

    def test_mount_unformatted_drive_fails(self):
        if not os.path.exists(h.SPARE_DEVICE):
            raise unittest.SkipTest("no spare drive %s" % h.SPARE_DEVICE)
        spare_mnt = h.MOUNTPOINT + "-spare"
        try:
            rc, out = h.mount(h.SPARE_DEVICE, spare_mnt, check=False)
            self.assertNotEqual(rc, 0, "mounting a never formatted drive must fail")
            self.assertFalse(h.is_mounted(spare_mnt))
            # and the same drive becomes usable after mkfs
            h.mkfs(h.SPARE_DEVICE)
            h.mount(h.SPARE_DEVICE, spare_mnt)
            self.assertEqual(os.listdir(spare_mnt), [])
        finally:
            h.umount(spare_mnt, check=False)
            if os.path.isdir(spare_mnt):
                os.rmdir(spare_mnt)

    def test_two_filesystems_are_independent(self):
        if not os.path.exists(h.SPARE_DEVICE):
            raise unittest.SkipTest("no spare drive %s" % h.SPARE_DEVICE)
        spare_mnt = h.MOUNTPOINT + "-spare"
        try:
            h.mkfs(h.DEVICE)
            h.mkfs(h.SPARE_DEVICE)
            h.mount(h.DEVICE, h.MOUNTPOINT)
            h.mount(h.SPARE_DEVICE, spare_mnt)
            h.write_file(os.path.join(h.MOUNTPOINT, "a"), b"main drive")
            h.write_file(os.path.join(spare_mnt, "b"), b"spare drive")
            self.assertEqual(os.listdir(h.MOUNTPOINT), ["a"])
            self.assertEqual(os.listdir(spare_mnt), ["b"])
            h.umount(spare_mnt)
            h.remount()
            h.mount(h.SPARE_DEVICE, spare_mnt)
            self.assertEqual(h.read_file(os.path.join(h.MOUNTPOINT, "a")), b"main drive")
            self.assertEqual(h.read_file(os.path.join(spare_mnt, "b")), b"spare drive")
        finally:
            h.umount(spare_mnt, check=False)
            if os.path.isdir(spare_mnt):
                os.rmdir(spare_mnt)

    def test_umount_busy(self):
        h.mkfs(h.DEVICE)
        h.mount()
        handle = open(os.path.join(h.MOUNTPOINT, "busy"), "wb")
        try:
            rc, _ = h.run(["umount", h.MOUNTPOINT], check=False)
            self.assertNotEqual(rc, 0, "umount with an open file must fail (EBUSY)")
            self.assertTrue(h.is_mounted())
        finally:
            handle.close()
        h.umount()
        self.assertFalse(h.is_mounted())

    def test_rmmod_while_mounted_fails(self):
        h.mkfs(h.DEVICE)
        h.mount()
        rc, _ = h.rmmod(check=False)
        self.assertNotEqual(rc, 0, "rmmod exofs while mounted must fail")
        self.assertTrue(h.module_loaded())
        self.assertTrue(h.is_mounted())

    def test_mount_on_missing_mountpoint_fails(self):
        h.mkfs(h.DEVICE)
        rc, _ = h.run(["mount", "-t", "exofs", h.DEVICE, "/mnt/exofs-no-such-dir"], check=False)
        self.assertNotEqual(rc, 0)

    @h.known_issue("exofs_show_options() prints 'TODO\\n', which breaks the "
                   "/proc/mounts line format (should print nothing or ',opt')")
    def test_proc_mounts_line_is_well_formed(self):
        h.mkfs(h.DEVICE)
        h.mount()
        for line in h.read_text("/proc/mounts").splitlines():
            fields = line.split()
            self.assertEqual(len(fields), 6, "malformed /proc/mounts line: %r" % line)


class TestKnownQemuIssues(unittest.TestCase):
    """Issues of the project qemu (not of exofs) found by this suite."""

    def tearDown(self):
        h.umount(check=False)
        if not h.KEEP_WRITE_CACHE:
            h.set_write_cache(h.DEVICE, "write through")

    @h.known_issue("qemu KV mode rejects NVMe Flush (Invalid Opcode) although the "
                   "controller reports a volatile write cache, so fsync() fails with "
                   "EOPNOTSUPP; nvme_kv_io_cmd() should handle NVME_CMD_FLUSH")
    def test_fsync_with_volatile_write_cache(self):
        h.enable_object_mode_everywhere()
        h.insmod()
        h.set_write_cache(h.DEVICE, "write back")
        self.assertEqual(h.get_write_cache(h.DEVICE), "write back")
        h.umount(check=False)
        h.mkfs(h.DEVICE)
        h.mount()
        h.write_file(os.path.join(h.MOUNTPOINT, "fsync-me"), b"data")   # fsyncs
