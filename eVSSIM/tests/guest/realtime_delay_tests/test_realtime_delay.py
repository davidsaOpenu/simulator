#!/usr/bin/env python
# Acceptance test for the REALTIME_DELAY mode. EVSSIM_RUNTIME_REALTIME_DELAY=1|0
# in the environment, the flag QEMU was started with, selects which assertion applies.

import json
import os
import re
import subprocess
import time

DEV = "/dev/nvme0n1"
MNT = "/mnt/realtime_delay"
SIZE_MB = 160
PAGE_SIZE = 4096
PER_PAGE_US = 982
READ_MB = 40
PER_READ_US = 132
# Upper bound on the median read completion latency: modelled read time plus guest and QEMU
# overhead. An unpatched QEMU main loop rounds every sleep up to a whole millisecond (~1008 us per
# read), far above this. The median, unlike the total, ignores a noisy neighbour's tail.
READ_UPPER_US = 500
OFF_FRACTION = 0.50
MODE = {"1": "on", "0": "off"}.get(os.environ.get("EVSSIM_RUNTIME_REALTIME_DELAY", ""), "")
PAGES = SIZE_MB * 1024 * 1024 / PAGE_SIZE
BOUND = PAGES * PER_PAGE_US / 1000000.0
READS = READ_MB * 1024 * 1024 / PAGE_SIZE
FIO_LOG = "/tmp/fio-realtime-delay.log"
# subprocess.DEVNULL is Python 3 only; the guest runs Python 2.7.
DEVNULL = open(os.devnull, "w")


def run_fio(name, rw, size_mb):
    """Returns (wall seconds, median completion latency in us)."""
    start = time.time()
    try:
        output = subprocess.check_output(
            ["fio", "--name=" + name, "--filename=%s/f" % MNT, "--rw=" + rw, "--bs=4k",
             "--size=%dm" % size_mb, "--ioengine=sync", "--iodepth=1", "--direct=1",
             "--output-format=json"])
    except subprocess.CalledProcessError as e:
        raise AssertionError("%s fio exited %d:\n%s" % (rw, e.returncode, e.output))
    elapsed = time.time() - start
    if not isinstance(output, str):
        output = output.decode("utf-8", "replace")
    open(FIO_LOG, "a").write(output)
    return elapsed, median_clat_us(json.loads(output), rw)


def median_clat_us(result, rw):
    stats = result["jobs"][0][rw]
    # fio 3 reports clat_ns in ns; fio 2 reports clat in us
    if "clat_ns" in stats:
        return stats["clat_ns"]["percentile"]["50.000000"] / 1000.0
    return float(stats["clat"]["percentile"]["50.000000"])


def mount_fresh_filesystem():
    subprocess.check_call(["dmesg", "-c"], stdout=DEVNULL)
    # Left mounted only if an earlier run died before its own umount.
    subprocess.call(["umount", MNT], stderr=DEVNULL)
    if not os.path.isdir(MNT):
        os.makedirs(MNT)
    subprocess.check_call(["mkfs.ext4", "-F", DEV], stdout=DEVNULL)
    subprocess.check_call(["mount", DEV, MNT])


def check_write_time(elapsed):
    print("ELAPSED_SECONDS: %.3f" % elapsed)
    print("BOUND_SECONDS: %.3f" % BOUND)
    if MODE == "on":
        assert elapsed >= BOUND, \
            "elapsed %.3fs is below the %.3fs lower bound: the delay did not happen" % (elapsed, BOUND)
    else:
        off_limit = BOUND * OFF_FRACTION
        print("OFF_LIMIT_SECONDS: %.3f" % off_limit)
        assert elapsed < off_limit, \
            "elapsed %.3fs reached %.3fs with the flag off" % (elapsed, off_limit)


def check_read_time(read_elapsed, read_median_us):
    read_lower = READS * PER_READ_US / 1000000.0
    print("READS: %d per_read=%dus" % (READS, PER_READ_US))
    print("READ_ELAPSED_SECONDS: %.3f (%.1f us per read)" % (read_elapsed, read_elapsed * 1e6 / READS))
    print("READ_LOWER_SECONDS: %.3f" % read_lower)
    print("READ_MEDIAN_US: %.1f (upper %d)" % (read_median_us, READ_UPPER_US))
    if MODE == "on":
        assert read_elapsed >= read_lower, \
            "reads took %.3fs, below the %.3fs lower bound: the delay did not happen" % (read_elapsed, read_lower)
        assert read_median_us <= READ_UPPER_US, \
            "median read latency %.1fus is above %dus: sleeps are rounded up" \
            " (is the QEMU main-loop fix applied?)" % (read_median_us, READ_UPPER_US)


class TestRealtimeDelay:
    # The workload runs once here so each test only checks its own result and
    # nose's alphabetical method order does not matter.
    @classmethod
    def setup_class(cls):
        assert MODE in ("on", "off"), \
            "EVSSIM_RUNTIME_REALTIME_DELAY must be 1 or 0, got %r" % os.environ.get("EVSSIM_RUNTIME_REALTIME_DELAY")
        print("device=%s size=%dMiB pages=%d per_page=%dus" % (DEV, SIZE_MB, PAGES, PER_PAGE_US))
        print("simulated flash time for the file data alone: %.3fs" % BOUND)

        mount_fresh_filesystem()
        open(FIO_LOG, "w").close()
        try:
            cls.write_elapsed, _ = run_fio("w", "write", SIZE_MB)
            # Reads: one 4 KiB page per command, straight from the device (direct I/O).
            cls.read_elapsed, cls.read_median_us = run_fio("r", "read", READ_MB)
        finally:
            subprocess.call(["umount", MNT])

    def test_write_spends_the_simulated_flash_time(self):
        check_write_time(self.write_elapsed)

    def test_read_spends_the_simulated_flash_time(self):
        check_read_time(self.read_elapsed, self.read_median_us)

    def test_kernel_log_is_clean(self):
        found = [line for line in subprocess.check_output(["dmesg"], universal_newlines=True).splitlines()
                 if re.search("WARNING:|BUG:|Oops|Call Trace", line, re.IGNORECASE)]
        assert not found, "kernel WARN/BUG/oops in dmesg:\n%s" % "\n".join(found)
