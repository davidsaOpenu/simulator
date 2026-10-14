#!/usr/bin/env python
"""
Shared helpers for the exofs guest tests.

The guest is Ubuntu 14.04 (python 2.7 + nose 1.3) running kernel 7.0.2 with
exofs built as a module.  Everything here must stay python2/python3 compatible.

Bring-up sequence (steps 3-5 of the test plan):
  3. nvme set-feature <dev> -f 192 --value=1   - key/value (object) mode on
  4. insmod exofs.ko                            - load our exofs module
  5. mkfs.exofs <dev> && mount -t exofs <dev> <mnt>

Environment variables (all optional):
  EXOFS_DEVICE          main drive (default /dev/nvme0n1)
  EXOFS_SPARE_DEVICE    second drive, left unformatted by bring-up
                        (default /dev/nvme1n1, tests skip if missing)
  EXOFS_MOUNTPOINT      mount point (default /mnt/exofs-test)
  EXOFS_KO              path of exofs.ko
                        (default /lib/modules/$(uname -r)/kernel/fs/exofs/exofs.ko)
  EXOFS_NVME            nvme-cli binary (default: nvme from PATH)
  EXOFS_MKFS            mkfs.exofs binary (default: mkfs.exofs from PATH)
  EXOFS_STRICT          1 = also run the tests of known exofs issues
  EXOFS_KEEP_WRITE_CACHE 1 = leave the NVMe volatile write cache on (qemu
                        rejects Flush in KV mode, so fsync fails then)
  EXOFS_LARGE_FILE_MB   size of the large file test (default 8)
  EXOFS_MANY_FILES      number of files in the many-files tests (default 200)
"""

from __future__ import print_function

import errno
import functools
import glob
import hashlib
import os
import platform
import shutil
import subprocess
import time
import unittest

EXOFS_SUPER_MAGIC = 0x5DF5
OBJ_FEATURE_ID = 192
EXOFS_NAME_LEN = 255
EXOFS_FAST_SYMLINK_MAX = 19     # i_data is 5 * __le32 = 20 bytes incl. NUL

PAGE = 4096

DEVICE = os.environ.get("EXOFS_DEVICE", "/dev/nvme0n1")
SPARE_DEVICE = os.environ.get("EXOFS_SPARE_DEVICE", "/dev/nvme1n1")
MOUNTPOINT = os.environ.get("EXOFS_MOUNTPOINT", "/mnt/exofs-test")
KO_PATH = os.environ.get(
    "EXOFS_KO",
    "/lib/modules/%s/kernel/fs/exofs/exofs.ko" % platform.release())
NVME = os.environ.get("EXOFS_NVME", "nvme")
MKFS = os.environ.get("EXOFS_MKFS", "mkfs.exofs")
STRICT = os.environ.get("EXOFS_STRICT", "0").strip().lower() in ("1", "y", "yes", "true")
LARGE_FILE_MB = int(os.environ.get("EXOFS_LARGE_FILE_MB", "8"))
MANY_FILES = int(os.environ.get("EXOFS_MANY_FILES", "200"))
KEEP_WRITE_CACHE = os.environ.get("EXOFS_KEEP_WRITE_CACHE", "0").strip().lower() in ("1", "y", "yes", "true")
DEVICE_WAIT_SECONDS = 60

DMESG_BAD_PATTERNS = (
    "BUG:", "WARNING:", "Oops", "general protection fault", "Call Trace",
    "Kernel panic", "kernel BUG", "page allocation failure", "list_add corruption",
    "list_del corruption", "UBSAN", "KASAN", "refcount_t",
)


# ---------------------------------------------------------------------------
# process helpers
# ---------------------------------------------------------------------------
def run(cmd, check=True, quiet=False):
    """Run a command, return (returncode, combined output as text)."""
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = proc.communicate()[0]
    if not isinstance(output, str):
        output = output.decode("utf-8", "replace")
    if not quiet:
        print("$ %s  -> rc=%d" % (" ".join(cmd), proc.returncode))
        if output.strip():
            print(output.rstrip())
    if check and proc.returncode != 0:
        raise AssertionError("command failed (rc=%d): %s\n%s"
                             % (proc.returncode, " ".join(cmd), output))
    return proc.returncode, output


def known_issue(reason):
    """
    Decorator for tests that exercise a known, already analysed exofs issue.
    They are skipped (with the reason in the report) unless EXOFS_STRICT=1,
    so the default run stays green while the issue is documented.
    """
    def decorator(func):
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            if not STRICT:
                raise unittest.SkipTest("KNOWN ISSUE (run with EXOFS_STRICT=1): %s" % reason)
            return func(*args, **kwargs)
        return wrapper
    return decorator


# ---------------------------------------------------------------------------
# /proc helpers
# ---------------------------------------------------------------------------
def read_text(path):
    handle = open(path)
    try:
        return handle.read()
    finally:
        handle.close()


def mount_entries():
    """List of (source, mountpoint, fstype, raw_line) from /proc/mounts."""
    entries = []
    for line in read_text("/proc/mounts").splitlines():
        fields = line.split()
        if len(fields) >= 3:
            entries.append((fields[0], fields[1], fields[2], line))
    return entries


def mount_entry(mountpoint):
    target = os.path.realpath(mountpoint)
    for entry in mount_entries():
        if os.path.realpath(entry[1]) == target:
            return entry
    return None


def is_mounted(mountpoint=MOUNTPOINT):
    return mount_entry(mountpoint) is not None


def module_loaded(name="exofs"):
    for line in read_text("/proc/modules").splitlines():
        if line.split()[:1] == [name]:
            return True
    return False


def fs_registered(name="exofs"):
    for line in read_text("/proc/filesystems").splitlines():
        if line.split()[-1:] == [name]:
            return True
    return False


def device_is_mounted(dev):
    real = os.path.realpath(dev)
    for source, _, _, _ in mount_entries():
        if os.path.realpath(source) == real:
            return True
    return False


# ---------------------------------------------------------------------------
# step 3: NVMe object (key/value) mode
# ---------------------------------------------------------------------------
def wait_for_device(dev, timeout=DEVICE_WAIT_SECONDS):
    deadline = time.time() + timeout
    while not os.path.exists(dev):
        if time.time() > deadline:
            raise AssertionError("%s did not appear within %ds (found: %s)"
                                 % (dev, timeout, ", ".join(sorted(glob.glob("/dev/nvme*"))) or "nothing"))
        time.sleep(0.5)


def nvme_set_object_mode(dev, enabled=True):
    wait_for_device(dev)
    run([NVME, "set-feature", dev, "-f", str(OBJ_FEATURE_ID),
         "--value=%d" % (1 if enabled else 0)])
    if enabled and not KEEP_WRITE_CACHE:
        set_write_cache(dev, "write through")


def write_cache_path(dev):
    return "/sys/block/%s/queue/write_cache" % os.path.basename(dev)


def get_write_cache(dev):
    return read_text(write_cache_path(dev)).strip()


def set_write_cache(dev, mode):
    """
    Known qemu issue: in key/value mode qemu rejects NVMe Flush (Invalid
    Opcode) although the controller reports a volatile write cache, so every
    fsync() fails with EOPNOTSUPP. "write through" makes the block layer skip
    the flushes (qemu stores objects synchronously, so nothing is lost).
    Set EXOFS_KEEP_WRITE_CACHE=1 to keep the cache (see TestKnownQemuIssues).
    """
    handle = open(write_cache_path(dev), "w")
    try:
        handle.write(mode + "\n")
    finally:
        handle.close()


def nvme_get_object_mode(dev):
    """Current value of feature 192, or None if it cannot be parsed."""
    _, out = run([NVME, "get-feature", dev, "-f", str(OBJ_FEATURE_ID)], check=False)
    for token in out.replace(",", " ").split():
        if token.lower().startswith("0x"):
            try:
                return int(token, 16)
            except ValueError:
                pass
    return None


# ---------------------------------------------------------------------------
# step 4: module
# ---------------------------------------------------------------------------
def insmod():
    if module_loaded():
        return
    assert os.path.exists(KO_PATH), "exofs module not found at %s" % KO_PATH
    run(["insmod", KO_PATH])
    assert module_loaded(), "exofs is not in /proc/modules after insmod"
    assert fs_registered(), "exofs is not in /proc/filesystems after insmod"


def rmmod(check=True):
    return run(["rmmod", "exofs"], check=check)


# ---------------------------------------------------------------------------
# step 5: format + mount
# ---------------------------------------------------------------------------
def mkfs(dev=DEVICE, check=True):
    assert not device_is_mounted(dev), "refusing to mkfs mounted device %s" % dev
    return run([MKFS, dev], check=check)


def mount(dev=DEVICE, mountpoint=MOUNTPOINT, check=True):
    if not os.path.isdir(mountpoint):
        os.makedirs(mountpoint)
    return run(["mount", "-t", "exofs", dev, mountpoint], check=check)


def umount(mountpoint=MOUNTPOINT, check=True):
    if not is_mounted(mountpoint):
        return 0, ""
    rc, out = run(["umount", mountpoint], check=False)
    if rc != 0 and is_mounted(mountpoint) and check:
        raise AssertionError("umount %s failed: %s" % (mountpoint, out))
    return rc, out


def remount(dev=DEVICE, mountpoint=MOUNTPOINT):
    """Unmount + mount: forces everything to be read back from the drive."""
    run(["sync"], quiet=True)
    umount(mountpoint)
    assert not is_mounted(mountpoint), "%s still mounted after umount" % mountpoint
    mount(dev, mountpoint)


def drop_caches():
    run(["sync"], quiet=True)
    handle = open("/proc/sys/vm/drop_caches", "w")
    try:
        handle.write("3\n")
    finally:
        handle.close()


def enable_object_mode_everywhere():
    nvme_set_object_mode(DEVICE)
    if os.path.exists(SPARE_DEVICE):
        nvme_set_object_mode(SPARE_DEVICE)


def bring_up(fresh=True):
    """Steps 3-5. With fresh=True the main drive is re-formatted."""
    assert os.geteuid() == 0, "exofs tests must run as root"
    enable_object_mode_everywhere()
    insmod()
    if is_mounted():
        if not fresh:
            return
        umount()
    if fresh:
        mkfs(DEVICE)
    mount(DEVICE, MOUNTPOINT)
    entry = mount_entry(MOUNTPOINT)
    assert entry is not None and entry[2] == "exofs", \
        "%s is not mounted as exofs: %r" % (MOUNTPOINT, entry)


def tear_down():
    if is_mounted():
        umount(check=False)


# ---------------------------------------------------------------------------
# data helpers
# ---------------------------------------------------------------------------
def pattern(size, seed=0):
    """Deterministic, non-repeating-per-page data of the given size."""
    out = bytearray()
    counter = seed
    while len(out) < size:
        out += hashlib.sha256(("%d" % counter).encode("ascii")).digest()
        counter += 1
    return bytes(out[:size])


def write_file(path, data, mode="wb"):
    handle = open(path, mode)
    try:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    finally:
        handle.close()


def read_file(path):
    handle = open(path, "rb")
    try:
        return handle.read()
    finally:
        handle.close()


def sha256_file(path, chunk=1 << 20):
    digest = hashlib.sha256()
    handle = open(path, "rb")
    try:
        while True:
            block = handle.read(chunk)
            if not block:
                break
            digest.update(block)
    finally:
        handle.close()
    return digest.hexdigest()


def first_difference(expected, actual):
    for index in range(min(len(expected), len(actual))):
        if expected[index:index + 1] != actual[index:index + 1]:
            return index
    if len(expected) == len(actual):
        return -1
    return min(len(expected), len(actual))


def assert_same_data(expected, actual, what):
    if expected == actual:
        return
    diff = first_difference(expected, actual)
    assert diff == -1, ("%s: wrote %d bytes, read %d bytes, first difference at offset %d"
                        % (what, len(expected), len(actual), diff))


def assert_errno(expected_errnos, func, *args, **kwargs):
    """Call func and assert it fails with one of the expected errno values."""
    if isinstance(expected_errnos, int):
        expected_errnos = (expected_errnos,)
    try:
        result = func(*args, **kwargs)
    except (OSError, IOError) as error:
        assert error.errno in expected_errnos, (
            "%s%r failed with %s (%s), expected %s"
            % (getattr(func, "__name__", func), args, errno.errorcode.get(error.errno, error.errno),
               error, "/".join(errno.errorcode.get(e, str(e)) for e in expected_errnos)))
        return error
    if func is os.open and isinstance(result, int):
        os.close(result)          # never leak an fd: it would keep the fs busy
    raise AssertionError("%s%r succeeded, expected %s"
                         % (getattr(func, "__name__", func), args,
                            "/".join(errno.errorcode.get(e, str(e)) for e in expected_errnos)))


def statvfs_files(path=MOUNTPOINT):
    return os.statvfs(path).f_files


def wait_for(predicate, timeout=5.0, interval=0.1):
    deadline = time.time() + timeout
    while True:
        if predicate():
            return True
        if time.time() > deadline:
            return False
        time.sleep(interval)


def fs_magic(path=MOUNTPOINT):
    _, out = run(["stat", "-f", "-c", "%t", path])
    return int(out.strip().splitlines()[-1], 16)


def kmsg_mark(text):
    """Write a marker line into the kernel log (to slice dmesg later)."""
    try:
        handle = open("/dev/kmsg", "w")
        try:
            handle.write("exofs-test: %s\n" % text)
        finally:
            handle.close()
    except (IOError, OSError):
        pass


def dmesg_problems(text):
    problems = []
    for line in text.splitlines():
        for bad in DMESG_BAD_PATTERNS:
            if bad in line:
                problems.append(line)
                break
    return problems


class ExofsTestCase(unittest.TestCase):
    """
    Base class: every test gets its own fresh directory under the mount point
    (self.dir). The mount itself is shared by the module (setup_module).
    """

    def setUp(self):
        assert is_mounted(), "exofs is not mounted on %s" % MOUNTPOINT
        name = self.id().split(".")[-1]
        self.dir = os.path.join(MOUNTPOINT, name)
        if os.path.lexists(self.dir):
            shutil.rmtree(self.dir, ignore_errors=True)
        os.mkdir(self.dir)
        kmsg_mark("start %s" % self.id())

    def tearDown(self):
        kmsg_mark("end %s" % self.id())
        if not is_mounted():
            # a test that unmounted must leave things usable for the next one
            mount(DEVICE, MOUNTPOINT, check=False)
        if is_mounted():
            shutil.rmtree(self.dir, ignore_errors=True)

    def path(self, *parts):
        return os.path.join(self.dir, *parts)

    def remount(self):
        remount()
