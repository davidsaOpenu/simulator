#!/usr/bin/env python

from __future__ import print_function

import glob
import os
import subprocess
import time

MOUNTS_PATH = "/proc/mounts"
MODULES_PATH = "/proc/modules"
DEVICE_GLOB = "/dev/nvme*n1"
DEVICE_WAIT_SECONDS = 30
PROVISIONED_DEVICES_ENV = "EVSSIM_PROVISIONED_DEVICE_COUNT"

# Must match docker/ssd.conf.filesystems.template: [nvme01] is object mode,
# [nvme02] sector mode. exofs only works on nvme0n1.
FILESYSTEMS = [
    {"fs": "exofs", "dev": "/dev/nvme0n1", "mountpoint": "/mnt/exofs0",
     "magic": "5df5", "lib": "/home/esd/exofs/exofs_lib.sh",
     "remount": None, "post_umount_checks": []},
    {"fs": "ext4", "dev": "/dev/nvme1n1", "mountpoint": "/mnt/ext4",
     "magic": "ef53", "lib": "/home/esd/ext4/ext4_lib.sh",
     "remount": ["mount", "-t", "ext4"],
     "post_umount_checks": [["fsck.ext4", "-f", "-n"]]},
]


def find_filesystem(name):
    # type: (str) -> dict
    for fs in FILESYSTEMS:
        if fs["fs"] == name:
            return fs
    raise ValueError("Unknown filesystem %r, expected one of: %s"
                     % (name, ", ".join(fs["fs"] for fs in FILESYSTEMS)))


def run(cmd, dev, action, check=True, env=None):
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


def run_fs_lib(fs, function, check=True):
    # type: (dict, str, bool) -> tuple
    env = dict(os.environ, NVME_DEV=fs["dev"], MOUNT_POINT=fs["mountpoint"])
    script = 'source "%s"; %s' % (fs["lib"], function)
    return run(["bash", "-c", script], fs["dev"],
               "%s %s" % (function, fs["fs"]), check=check, env=env)


def _unescape_mount_field(value):
    # type: (str) -> str
    return value.replace("\\040", " ").replace("\\011", "\t").replace("\\134", "\\")


def read_mounts():
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


def is_mounted(mountpoint):
    # type: (str) -> bool
    target = os.path.realpath(mountpoint)
    for entry in read_mounts():
        if os.path.realpath(entry[1]) == target:
            return True
    return False


def module_is_loaded(name):
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


def load_nvme_driver():
    # type: () -> None
    run(["rmmod", "dnvme"], "dnvme",
        "unload the conflicting dnvme driver", check=False)
    run(["modprobe", "nvme"], "nvme", "load the stock nvme driver")
    assert module_is_loaded("nvme"), (
        "The nvme module is not listed in %s after modprobe" % MODULES_PATH)


def assert_device_is_free(dev):
    # type: (str) -> None
    # mkfs here is destructive and unrecoverable, so refuse anything in use.
    # Match by name and by device id: the root fs is often listed as /dev/root
    # or by UUID, in which case the name comparison alone misses it.
    real_dev = os.path.realpath(dev)
    try:
        dev_id = os.stat(dev).st_rdev
    except OSError:
        dev_id = None

    for source, mountpoint in read_mounts():
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


def provisioned_device_count():
    # type: () -> int
    raw = os.environ.get(PROVISIONED_DEVICES_ENV, "").strip()
    assert raw.isdigit() and int(raw) > 0, (
        "%s must be set to the number of provisioned NVMe drives (got %r); "
        "docker-test-filesystems.sh sets it from the QEMU configuration"
        % (PROVISIONED_DEVICES_ENV, raw))
    return int(raw)


def wait_for_devices(expected, timeout=DEVICE_WAIT_SECONDS):
    # type: (int, int) -> list
    # setup reloads the nvme driver, so udev may not have made the nodes yet
    deadline = time.time() + timeout
    while True:
        devices = sorted(glob.glob(DEVICE_GLOB))
        if len(devices) >= expected or time.time() >= deadline:
            return devices
        time.sleep(0.5)


def assert_all_devices_present():
    # type: () -> list
    expected = provisioned_device_count()
    nvme_devices = wait_for_devices(expected)
    assert len(nvme_devices) == expected, (
        "Expected {expected} NVMe drive(s) but found {found} after "
        "{seconds} seconds: {devices}"
        .format(expected=expected, found=len(nvme_devices),
                seconds=DEVICE_WAIT_SECONDS,
                devices=", ".join(nvme_devices) or "nothing"))
    return nvme_devices
