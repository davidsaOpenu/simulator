#!/usr/bin/env python

from __future__ import print_function

import errno
import glob
import os
import stat
import subprocess
import tempfile
import time

MOUNTS_PATH = "/proc/mounts"
MODULES_PATH = "/proc/modules"
DEVICE_GLOB = "/dev/nvme*n1"
DEVICE_WAIT_SECONDS = 30
TEST_FILE_NAME = "evssim_roundtrip.txt"
PAYLOAD_REPEAT = 2048


def _run(cmd, dev, action, check=True):
    # type: (list, str, str, bool) -> tuple
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = proc.communicate()[0]
    if not isinstance(output, str):
        output = output.decode("utf-8", "replace")
    print("[%s] $ %s" % (dev, " ".join(cmd)))
    if output.strip():
        print(output.rstrip())
    if check:
        assert proc.returncode == 0, (
            "%s failed on %s with exit code %d while trying to %s:\n%s"
            % (cmd[0], dev, proc.returncode, action, output))
    return proc.returncode, output


def _unescape_mount_field(value):
    # type: (str) -> str
    return value.replace("\\040", " ").replace("\\011", "\t").replace("\\134", "\\")


def _read_mounts():
    # type: () -> list
    entries = []
    handle = open(MOUNTS_PATH)
    try:
        for line in handle:
            fields = line.split()
            if len(fields) < 2:
                continue
            entries.append((_unescape_mount_field(fields[0]),
                            _unescape_mount_field(fields[1])))
    finally:
        handle.close()
    return entries


def _is_mounted(mountpoint):
    # type: (str) -> bool
    target = os.path.realpath(mountpoint)
    for entry in _read_mounts():
        if os.path.realpath(entry[1]) == target:
            return True
    return False


def _module_is_loaded(name):
    # type: (str) -> bool
    handle = open(MODULES_PATH)
    try:
        for line in handle:
            fields = line.split()
            if fields and fields[0] == name:
                return True
    finally:
        handle.close()
    return False


def _assert_device_is_free(dev):
    # type: (str) -> None
    # mkfs here is destructive and unrecoverable, so refuse anything in use.
    # Match by name and by device id: the root fs is often listed as /dev/root
    # or by UUID, in which case the name comparison alone misses it.
    real_dev = os.path.realpath(dev)
    try:
        dev_id = os.stat(dev).st_rdev
    except OSError:
        dev_id = None

    for source, mountpoint in _read_mounts():
        real_source = os.path.realpath(source)
        matched_by_name = (real_source == real_dev or
                           real_source.startswith(real_dev + "p"))
        matched_by_id = False
        if dev_id is not None:
            try:
                matched_by_id = os.stat(mountpoint).st_dev == dev_id
            except OSError:
                matched_by_id = False
        assert not (matched_by_name or matched_by_id), (
            "Refusing to run a destructive mkfs on %s: it is in use -- %s is "
            "mounted on %s. This test may only be run against the simulated "
            "vssim drives." % (dev, source, mountpoint))


def _wait_for_devices(timeout=DEVICE_WAIT_SECONDS):
    # type: (int) -> list
    # setup reloads the nvme driver, so udev may not have made the nodes yet
    deadline = time.time() + timeout
    while True:
        devices = sorted(glob.glob(DEVICE_GLOB))
        if devices or time.time() >= deadline:
            return devices
        time.sleep(0.5)


def _payload(dev):
    # type: (str) -> str
    # tagged with the device so a stale file cannot compare equal by accident
    return ("eVSSIM ext4 round trip on %s\n" % dev) * PAYLOAD_REPEAT


def _first_mismatch(expected, actual):
    # type: (str, str) -> int
    limit = min(len(expected), len(actual))
    for index in range(limit):
        if expected[index] != actual[index]:
            return index
    if len(expected) == len(actual):
        return -1
    return limit


class TestExt4Format:

    # nose resolves per-test fixtures via ('setup', 'setUp') only; setup_method
    # is a pytest name it ignores. The *_method aliases keep pytest 8 working,
    # which in turn no longer honours the nose names.
    def setup(self):
        # type: () -> None
        self._mountpoints = []
        _run(["rmmod", "dnvme"], "dnvme",
             "unload the conflicting dnvme driver", check=False)
        _run(["modprobe", "nvme"], "nvme", "load the stock nvme driver")
        assert _module_is_loaded("nvme"), (
            "The nvme module is not listed in %s after modprobe" % MODULES_PATH)

    def teardown(self):
        # type: () -> None
        for mountpoint in reversed(getattr(self, "_mountpoints", [])):
            self._release(mountpoint)
        self._mountpoints = []

    def setup_method(self, _method=None):
        # type: (object) -> None
        self.setup()

    def teardown_method(self, _method=None):
        # type: (object) -> None
        self.teardown()

    def _release(self, mountpoint):
        # type: (str) -> None
        if _is_mounted(mountpoint):
            _run(["umount", mountpoint], mountpoint,
                 "unmount a leftover mount point", check=False)
        if _is_mounted(mountpoint):
            _run(["umount", "-l", mountpoint], mountpoint,
                 "lazily unmount a stuck mount point", check=False)
        try:
            os.rmdir(mountpoint)
        except OSError as error:
            if error.errno != errno.ENOENT:
                print("WARNING could not remove %s: %s" % (mountpoint, error))

    def _check_drive(self, dev):
        # type: (str) -> None
        try:
            mode = os.stat(dev).st_mode
        except OSError as error:
            raise AssertionError("Cannot stat %s: %s" % (dev, error))
        assert stat.S_ISBLK(mode), "%s is not a block device" % dev
        _assert_device_is_free(dev)

        _run(["mkfs.ext4", "-F", dev], dev, "create an ext4 filesystem")
        _run(["dumpe2fs", "-h", dev], dev, "read the ext4 superblock back")

        payload = _payload(dev)
        mountpoint = tempfile.mkdtemp(prefix="evssim-ext4-")
        # recorded before the mount so teardown also cleans up a failed mount
        self._mountpoints.append(mountpoint)
        test_file = os.path.join(mountpoint, TEST_FILE_NAME)
        try:
            _run(["mount", "-t", "ext4", dev, mountpoint], dev,
                 "mount the newly created filesystem")

            handle = open(test_file, "w")
            try:
                handle.write(payload)
                handle.flush()
                os.fsync(handle.fileno())
            finally:
                handle.close()
            _run(["sync"], dev, "flush the page cache to the device")
            _run(["umount", mountpoint], dev,
                 "unmount the filesystem after writing")

            _run(["mount", "-t", "ext4", dev, mountpoint], dev,
                 "remount the filesystem to read the data back")
            handle = open(test_file, "r")
            try:
                read_back = handle.read()
            finally:
                handle.close()

            mismatch = _first_mismatch(payload, read_back)
            assert mismatch == -1, (
                "Data written to %s did not survive the unmount/remount cycle: "
                "wrote %d bytes, read %d bytes back, first difference at "
                "offset %d" % (dev, len(payload), len(read_back), mismatch))
        finally:
            self._release(mountpoint)

        # must run unmounted, hence after the block above
        _run(["fsck.ext4", "-f", "-n", dev], dev,
             "verify the filesystem is consistent")

    def test_format_all(self):
        # type: () -> None
        nvme_devices = _wait_for_devices()
        assert nvme_devices, (
            "No NVMe namespace devices matched %s after %d seconds; the nvme "
            "driver did not bind the simulated vssim drives"
            % (DEVICE_GLOB, DEVICE_WAIT_SECONDS))
        for nvme_device in nvme_devices:
            self._check_drive(nvme_device)
