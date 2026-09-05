#!/usr/bin/env python
# Acceptance test for the REALTIME_DELAY mode. MODE=on|off in the environment
# selects which assertion applies.

import os
import subprocess
import time

DEV = os.environ.get("DEV", "/dev/nvme0n1")
MNT = os.environ.get("MNT", "/mnt/realtime_delay")
SIZE_MB = int(os.environ.get("SIZE_MB", "160"))
PAGE_SIZE = int(os.environ.get("PAGE_SIZE", "4096"))
PER_PAGE_US = int(os.environ.get("PER_PAGE_US", "982"))
OFF_FRACTION = float(os.environ.get("OFF_FRACTION", "0.50"))
MODE = os.environ.get("MODE", "")
PAGES = SIZE_MB * 1024 * 1024 / PAGE_SIZE
BOUND = PAGES * PER_PAGE_US / 1000000.0
FIO_LOG = "/tmp/fio-realtime-delay.log"


def shell(command):
    proc = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = proc.communicate()[0]
    return proc.returncode, output


class TestRealtimeDelay:
    def test_a_write_spends_the_simulated_flash_time(self):
        assert MODE in ("on", "off"), "MODE must be on or off, got %r" % MODE
        print("device=%s size=%dMiB pages=%d per_page=%dus" % (DEV, SIZE_MB, PAGES, PER_PAGE_US))
        print("simulated flash time for the file data alone: %.3fs" % BOUND)

        shell("dmesg -c")
        shell("umount %s" % MNT)
        assert 0 == os.system("mkdir -p %s" % MNT)
        assert 0 == shell("mkfs.ext4 -F %s" % DEV)[0], "mkfs.ext4 failed"
        assert 0 == os.system("mount %s %s" % (DEV, MNT)), "mount failed"

        start = time.time()
        fio_rc, fio_output = shell(
            "fio --name=w --filename=%s/f --rw=write --bs=4k --size=%dm"
            " --ioengine=sync --iodepth=1 --direct=1" % (MNT, SIZE_MB))
        elapsed = time.time() - start
        open(FIO_LOG, "w").write(fio_output)

        print("ELAPSED_SECONDS: %.3f" % elapsed)
        print("BOUND_SECONDS: %.3f" % BOUND)
        assert 0 == fio_rc, "fio exited %d:\n%s" % (fio_rc, fio_output)

        try:
            if MODE == "on":
                assert elapsed >= BOUND, \
                    "elapsed %.3fs is below the %.3fs lower bound: the delay did not happen" % (elapsed, BOUND)
            else:
                off_limit = BOUND * OFF_FRACTION
                print("OFF_LIMIT_SECONDS: %.3f" % off_limit)
                assert elapsed < off_limit, \
                    "elapsed %.3fs reached %.3fs with the flag off" % (elapsed, off_limit)
        finally:
            os.system("umount %s" % MNT)

    def test_b_kernel_log_is_clean(self):
        found = shell("dmesg | grep -iE 'WARNING:|BUG:|Oops|Call Trace'")[1]
        assert found == "", "kernel WARN/BUG/oops in dmesg:\n%s" % found
