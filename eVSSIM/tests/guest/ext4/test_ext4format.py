#!/usr/bin/env python

import os

NVME_DEVICES = ["/dev/nvme0n1", "/dev/nvme1n1", "/dev/nvme2n1"]  # type: list[str]


def check_drive(dev):
    # type: (str) -> None
    assert 0 == os.system("test -b %s" % dev)
    assert 0 == os.system("sudo mkfs.ext4 -F %s" % dev)
    assert 0 == os.system("sudo dumpe2fs -h %s" % dev)


class TestExt4Format:
    def test_setup(self):
        # type: () -> None
        os.system("rmmod dnvme 2>/dev/null || true")
        assert 0 == os.system("modprobe nvme")
        assert 0 == os.system("lsmod | grep -i nvme")

    def test_format_all(self):
        # type: () -> None
        for nvme_device in NVME_DEVICES:
            check_drive(nvme_device)
