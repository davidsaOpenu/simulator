#!/usr/bin/env python

import glob
import os


def check_drive(dev):
    # type: (str) -> None
    assert 0 == os.system("test -b %s" % dev)
    assert 0 == os.system("sudo mkfs.ext4 -F %s" % dev)
    assert 0 == os.system("sudo dumpe2fs -h %s" % dev)


class TestExt4Format:
    def setup_method(self):
        # type: () -> None
        os.system("rmmod dnvme 2>/dev/null || true")
        assert 0 == os.system("modprobe nvme")
        assert 0 == os.system("lsmod | grep -i nvme")

    def test_format_all(self):
        # type: () -> None
        nvme_devices = sorted(glob.glob("/dev/nvme*n1"))
        assert nvme_devices, "No NVMe namespace devices found under /dev/"
        for nvme_device in nvme_devices:
            check_drive(nvme_device)
