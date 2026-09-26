#!/usr/bin/env python

from __future__ import print_function

import glob
import os
import stat
import subprocess
import time

MOUNTS_PATH = "/proc/mounts"
MODULES_PATH = "/proc/modules"
DEVICE_GLOB = "/dev/nvme*n1"
DEVICE_WAIT_SECONDS = 30
PROVISIONED_DEVICES_ENV = "EVSSIM_PROVISIONED_DEVICE_COUNT"
TEST_FILE_NAME = "evssim_roundtrip.txt"
PAYLOAD_REPEAT = 2048
# iscsiadm exits 21 ("no objects found") when there are no sessions at all
ISCSI_NO_SESSIONS = 21
# mkfs.ext4 creates this; anything else at the root survived from a prior run
FRESH_ROOT_ENTRIES = set(["lost+found"])

# Must match docker/ssd.conf.filesystems.template: [nvme01] is object mode,
# [nvme02] sector mode. exofs only works on nvme0n1.
FILESYSTEMS = [
    {"fs": "exofs", "dev": "/dev/nvme0n1", "mountpoint": "/mnt/exofs0",
     "magic": "5df5", "lib": "/home/esd/exofs/exofs_lib.sh"},
    {"fs": "ext4", "dev": "/dev/nvme1n1", "mountpoint": "/mnt/ext4",
     "magic": "ef53", "lib": "/home/esd/ext4/ext4_lib.sh"},
]


def _run(cmd, dev, action, check=True, env=None):
    # type: (list, str, str, bool, dict) -> tuple
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, env=env,
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


def _run_fs_lib(fs, function, check=True):
    # type: (dict, str, bool) -> tuple
    env = dict(os.environ, NVME_DEV=fs["dev"], MOUNT_POINT=fs["mountpoint"])
    script = 'source "%s"; %s' % (fs["lib"], function)
    return _run(["bash", "-c", script], fs["dev"],
                "%s %s" % (function, fs["fs"]), check=check, env=env)


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


def _provisioned_device_count():
    # type: () -> int
    raw = os.environ.get(PROVISIONED_DEVICES_ENV, "").strip()
    assert raw.isdigit() and int(raw) > 0, (
        "%s must be set to the number of provisioned NVMe drives (got %r); "
        "docker-test-filesystems.sh sets it from the QEMU configuration"
        % (PROVISIONED_DEVICES_ENV, raw))
    return int(raw)


def _wait_for_devices(expected, timeout=DEVICE_WAIT_SECONDS):
    # type: (int, int) -> list
    # setup reloads the nvme driver, so udev may not have made the nodes yet
    deadline = time.time() + timeout
    while True:
        devices = sorted(glob.glob(DEVICE_GLOB))
        if len(devices) >= expected or time.time() >= deadline:
            return devices
        time.sleep(0.5)


def _payload(dev):
    # type: (str) -> str
    # tagged with the device so a stale file cannot compare equal by accident
    return ("eVSSIM round trip on %s\n" % dev) * PAYLOAD_REPEAT


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
        # unchecked: the residue checks in the test catch a failed teardown
        for fs in reversed(getattr(self, "_mountpoints", [])):
            _run_fs_lib(fs, "teardown_" + fs["fs"], check=False)
        self._mountpoints = []

    def setup_method(self, _method=None):
        # type: (object) -> None
        self.setup()

    def teardown_method(self, _method=None):
        # type: (object) -> None
        self.teardown()

    def _setup_drive(self, fs):
        # type: (dict) -> None
        dev = fs["dev"]
        try:
            mode = os.stat(dev).st_mode
        except OSError as error:
            raise AssertionError("Cannot stat %s: %s" % (dev, error))
        assert stat.S_ISBLK(mode), "%s is not a block device" % dev
        _assert_device_is_free(dev)

        # recorded before the mount so teardown also cleans up a failed mount
        self._mountpoints.append(fs)
        # formats the device, so every run starts from an empty one
        _run_fs_lib(fs, "setup_" + fs["fs"])

        _rc, output = _run(["stat", "-fc", "%t", fs["mountpoint"]], dev,
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
        handle = open(test_file, "w")
        try:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        finally:
            handle.close()
        _run(["sync"], dev, "flush the page cache to the device")
        _run(["sh", "-c", "echo 3 > /proc/sys/vm/drop_caches"], dev,
             "drop the clean page cache")
        handle = open(test_file, "r")
        try:
            read_back = handle.read()
        finally:
            handle.close()

        mismatch = _first_mismatch(payload, read_back)
        assert mismatch == -1, (
            "Data written to %s did not read back intact: wrote %d bytes, "
            "read %d bytes back, first difference at offset %d"
            % (dev, len(payload), len(read_back), mismatch))

    def _assert_no_residue(self):
        # type: () -> None
        mounts = _read_mounts()
        nvme_mounts = [entry for entry in mounts if "nvme" in entry[0]]
        assert not nvme_mounts, (
            "NVMe devices are still mounted after teardown: %r" % (nvme_mounts,))
        # exofs mounts from /dev/osd0, which the check above cannot see
        for fs in FILESYSTEMS:
            assert not _is_mounted(fs["mountpoint"]), (
                "%s is still mounted after teardown" % fs["mountpoint"])
        rc, output = _run(["iscsiadm", "-m", "session"], "iscsi",
                          "list remaining iSCSI sessions", check=False)
        assert rc == ISCSI_NO_SESSIONS or (rc == 0 and not output.strip()), (
            "iSCSI sessions survived teardown (exit %d):\n%s" % (rc, output))

    def test_format_all(self):
        # type: () -> None
        expected = _provisioned_device_count()
        nvme_devices = _wait_for_devices(expected)
        assert len(nvme_devices) == expected, (
            "Expected {expected} NVMe drive(s) but found {found} after "
            "{seconds} seconds: {devices}"
            .format(expected=expected, found=len(nvme_devices),
                    seconds=DEVICE_WAIT_SECONDS,
                    devices=", ".join(nvme_devices) or "nothing"))
        for fs in FILESYSTEMS:
            self._setup_drive(fs)
        for fs in FILESYSTEMS:
            self._check_drive(fs)

        self.teardown()
        self._assert_no_residue()
        # one-line recap so a log review does not have to re-derive the device
        # list from the interleaved subprocess output above
        print("Formatted and verified %d device(s): %s"
              % (len(nvme_devices), ", ".join(nvme_devices)))
