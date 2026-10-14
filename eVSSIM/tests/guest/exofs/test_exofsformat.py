#!/usr/bin/env python

from __future__ import print_function

import errno
import glob
import os
import stat
import subprocess
import tempfile
import time

import exofs_helpers

MOUNTS_PATH = "/proc/mounts"
MODULES_PATH = "/proc/modules"
DEVICE_GLOB = "/dev/nvme*n1"
DEVICE_WAIT_SECONDS = 30
PROVISIONED_DEVICES_ENV = "EVSSIM_PROVISIONED_DEVICE_COUNT"
TEST_FILE_NAME = "evssim_exofs_roundtrip.txt"
PAYLOAD_REPEAT = 2048


def _run(cmd, dev, action, check=True):
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


def _read_mounts():
    entries = []
    handle = open(MOUNTS_PATH)
    try:
        for line in handle:
            fields = line.split()
            if len(fields) >= 2:
                entries.append((fields[0], fields[1]))
    finally:
        handle.close()
    return entries


def _is_mounted(mountpoint):
    target = os.path.realpath(mountpoint)
    for _, mounted_path in _read_mounts():
        if os.path.realpath(mounted_path) == target:
            return True
    return False


def _module_is_loaded(name):
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
    real_dev = os.path.realpath(dev)

    for source, mountpoint in _read_mounts():
        real_source = os.path.realpath(source)
        if real_source == real_dev or real_source.startswith(real_dev + "p"):
            raise AssertionError(
                "Refusing to run mkfs.exofs on %s: %s is mounted on %s"
                % (dev, source, mountpoint))


def _provisioned_device_count():
    raw = os.environ.get(PROVISIONED_DEVICES_ENV, "").strip()
    assert raw.isdigit() and int(raw) > 0, (
        "%s must be set to the number of provisioned NVMe drives"
        % PROVISIONED_DEVICES_ENV)
    return int(raw)


def _wait_for_devices(expected, timeout=DEVICE_WAIT_SECONDS):
    deadline = time.time() + timeout
    while True:
        devices = sorted(glob.glob(DEVICE_GLOB))
        if len(devices) >= expected or time.time() >= deadline:
            return devices
        time.sleep(0.5)


def _payload(dev):
    return ("eVSSIM EXOFS round trip on %s\n" % dev) * PAYLOAD_REPEAT


def _first_mismatch(expected, actual):
    limit = min(len(expected), len(actual))
    for index in range(limit):
        if expected[index] != actual[index]:
            return index
    if len(expected) == len(actual):
        return -1
    return limit


class TestExofsFormat:

    def setup(self):
        self._mountpoints = []

        # exofs stores its objects through the NVMe key/value commands, so the
        # object feature must be enabled on every drive before mkfs.exofs, and
        # the module comes from the 7.0.2 build (insmod, not the distro modprobe)
        for nvme_device in sorted(glob.glob(DEVICE_GLOB)):
            exofs_helpers.nvme_set_object_mode(nvme_device, True)
        exofs_helpers.insmod()

        assert _module_is_loaded("exofs"), (
            "The exofs module is not listed in %s after insmod"
            % MODULES_PATH)

    def teardown(self):
        for mountpoint in reversed(getattr(self, "_mountpoints", [])):
            self._release(mountpoint)
        self._mountpoints = []

    def setup_method(self, _method=None):
        self.setup()

    def teardown_method(self, _method=None):
        self.teardown()

    def _release(self, mountpoint):
        if _is_mounted(mountpoint):
            _run(["umount", mountpoint], mountpoint,
                 "unmount EXOFS", check=False)
        if _is_mounted(mountpoint):
            _run(["umount", "-l", mountpoint], mountpoint,
                 "lazily unmount EXOFS", check=False)
        try:
            os.rmdir(mountpoint)
        except OSError as error:
            if error.errno != errno.ENOENT:
                print("WARNING could not remove %s: %s"
                      % (mountpoint, error))

    def _check_drive(self, dev):
        try:
            mode = os.stat(dev).st_mode
        except OSError as error:
            raise AssertionError("Cannot stat %s: %s" % (dev, error))

        assert stat.S_ISBLK(mode), "%s is not a block device" % dev
        _assert_device_is_free(dev)

        _run(["mkfs.exofs", dev], dev,
             "create an EXOFS filesystem")

        mountpoint = tempfile.mkdtemp(prefix="evssim-exofs-")
        self._mountpoints.append(mountpoint)
        test_file = os.path.join(mountpoint, TEST_FILE_NAME)
        payload = _payload(dev)

        try:
            _run(["mount", "-t", "exofs", dev, mountpoint], dev,
                 "mount the EXOFS filesystem")

            with open(test_file, "w") as handle:
                handle.write(payload)
                handle.flush()
                os.fsync(handle.fileno())

            _run(["sync"], dev, "flush EXOFS data")

            _run(["umount", mountpoint], dev,
                 "unmount EXOFS after writing")

            _run(["mount", "-t", "exofs", dev, mountpoint], dev,
                 "remount EXOFS to read the data back")

            with open(test_file, "r") as handle:
                read_back = handle.read()

            mismatch = _first_mismatch(payload, read_back)
            assert mismatch == -1, (
                "Data written to %s did not survive the "
                "unmount/remount cycle: wrote %d bytes, read %d bytes "
                "back, first difference at offset %d"
                % (dev, len(payload), len(read_back), mismatch))

        finally:
            self._release(mountpoint)

    def test_format_all(self):
        expected = _provisioned_device_count()
        nvme_devices = _wait_for_devices(expected)

        assert len(nvme_devices) == expected, (
            "Expected %d NVMe drive(s) but found %d after %d seconds: %s"
            % (expected, len(nvme_devices), DEVICE_WAIT_SECONDS,
               ", ".join(nvme_devices) or "nothing"))

        for nvme_device in nvme_devices:
            self._check_drive(nvme_device)

        print("Formatted and verified %d EXOFS device(s): %s"
              % (len(nvme_devices), ", ".join(nvme_devices)))
