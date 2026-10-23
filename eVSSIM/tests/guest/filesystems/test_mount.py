#!/usr/bin/env python

from __future__ import print_function

import os
import stat

from fs_common import (FILESYSTEMS, assert_all_devices_present,
                       assert_device_is_free, is_mounted, load_nvme_driver,
                       read_mounts, run, run_fs_lib)

TEST_FILE_NAME = "evssim_roundtrip.txt"
PAYLOAD_REPEAT = 2048
# iscsiadm exits 21 ("no objects found") when there are no sessions at all
ISCSI_NO_SESSIONS = 21
# mkfs.ext4 creates this; anything else at the root survived from a prior run
FRESH_ROOT_ENTRIES = set(["lost+found"])


def _payload(dev):
    # type: (str) -> str
    # tagged with the device so a stale file cannot compare equal by accident
    return ("eVSSIM round trip on %s\n" % dev) * PAYLOAD_REPEAT


def _assert_reads_back(test_file, dev, payload, stage):
    # type: (str, str, str, str) -> None
    handle = open(test_file, "r")
    try:
        read_back = handle.read()
    finally:
        handle.close()
    mismatch = _first_mismatch(payload, read_back)
    assert mismatch == -1, (
        "Data written to %s did not read back intact %s: "
        "wrote %d bytes, read %d bytes back, first difference at "
        "offset %d" % (dev, stage, len(payload), len(read_back), mismatch))


def _first_mismatch(expected, actual):
    # type: (str, str) -> int
    limit = min(len(expected), len(actual))
    for index in range(limit):
        if expected[index] != actual[index]:
            return index
    if len(expected) == len(actual):
        return -1
    return limit


class TestFilesystemMount:

    def setup(self):
        # type: () -> None
        self._mountpoints = []
        load_nvme_driver()

    def teardown(self):
        # type: () -> None
        failed = []
        for fs in reversed(getattr(self, "_mountpoints", [])):
            rc, _output = run_fs_lib(fs, "teardown_" + fs["fs"], check=False)
            if rc != 0:
                failed.append(fs)
        self._mountpoints = failed

    def _setup_drive(self, fs):
        # type: (dict) -> None
        dev = fs["dev"]
        try:
            mode = os.stat(dev).st_mode
        except OSError as error:
            raise AssertionError("Cannot stat %s: %s" % (dev, error))
        assert stat.S_ISBLK(mode), "%s is not a block device" % dev
        assert_device_is_free(dev)

        # recorded before the mount so teardown also cleans up a failed mount
        self._mountpoints.append(fs)
        # formats the device, so every run starts from an empty one
        run_fs_lib(fs, "setup_" + fs["fs"])

        _rc, output = run(["stat", "-fc", "%t", fs["mountpoint"]], dev,
                           "read the filesystem magic number")
        assert output.strip() == fs["magic"], (
            "%s on %s reports magic 0x%s, expected 0x%s"
            % (fs["fs"], fs["mountpoint"], output.strip(), fs["magic"]))
        leftovers = set(os.listdir(fs["mountpoint"])) - FRESH_ROOT_ENTRIES
        assert not leftovers, (
            "%s on %s is not freshly formatted, its root already holds: %s"
            % (fs["fs"], dev, ", ".join(sorted(leftovers))))

    def _check_drive(self, fs):
        # type: (dict) -> None
        dev = fs["dev"]
        payload = _payload(dev)
        test_file = os.path.join(fs["mountpoint"], TEST_FILE_NAME)
        try:
            handle = open(test_file, "w")
            try:
                handle.write(payload)
                handle.flush()
                os.fsync(handle.fileno())
            finally:
                handle.close()
            run(["sync"], dev, "flush the page cache to the device")
            run(["sh", "-c", "echo 3 > /proc/sys/vm/drop_caches"], dev,
                 "drop the clean page cache")
            _assert_reads_back(test_file, dev, payload,
                               "after dropping the page cache")

            # ext4 only: exofs mounts through /dev/osd0, not its NVMe device
            if fs["remount"]:
                run(["umount", fs["mountpoint"]], dev,
                     "unmount the filesystem after writing")
                run(fs["remount"] + [dev, fs["mountpoint"]], dev,
                     "remount the filesystem to read the data back")
                _assert_reads_back(test_file, dev, payload,
                                   "after an unmount/remount cycle")
        finally:
            # unchecked so it cannot mask a failure above
            rc, _output = run_fs_lib(fs, "teardown_" + fs["fs"], check=False)
            if rc == 0:
                self._mountpoints.remove(fs)
        assert rc == 0, (
            "teardown_%s failed on %s with exit code %d; %s may still be "
            "mounted or busy" % (fs["fs"], dev, rc, fs["mountpoint"]))

        # must run unmounted, hence after the block above
        for cmd in fs["post_umount_checks"]:
            run(cmd + [dev], dev, "verify the unmounted filesystem")

    def _assert_no_residue(self):
        # type: () -> None
        mounts = read_mounts()
        nvme_mounts = [entry for entry in mounts if "nvme" in entry[0]]
        assert not nvme_mounts, (
            "NVMe devices are still mounted after teardown: %r" % (nvme_mounts,))
        # exofs mounts from /dev/osd0, which the check above cannot see
        for fs in FILESYSTEMS:
            assert not is_mounted(fs["mountpoint"]), (
                "%s is still mounted after teardown" % fs["mountpoint"])
        rc, output = run(["iscsiadm", "-m", "session"], "iscsi",
                          "list remaining iSCSI sessions", check=False)
        assert rc == ISCSI_NO_SESSIONS or (rc == 0 and not output.strip()), (
            "iSCSI sessions survived teardown (exit %d):\n%s" % (rc, output))

    def test_mount_filesystems(self):
        # type: () -> None
        nvme_devices = assert_all_devices_present()
        for fs in FILESYSTEMS:
            self._setup_drive(fs)
        for fs in FILESYSTEMS:
            self._check_drive(fs)
        self._assert_no_residue()
        # one-line recap so a log review does not have to re-derive the device
        # list from the interleaved subprocess output above
        print("Formatted and verified %d device(s): %s"
              % (len(nvme_devices), ", ".join(nvme_devices)))
