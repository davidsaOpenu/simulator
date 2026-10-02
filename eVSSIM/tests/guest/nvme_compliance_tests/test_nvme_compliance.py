#!/usr/bin/env python

import os
import time
import subprocess
import unittest

VSSIM_NEXTGEN_BUILD_SYSTEM = "VSSIM_NEXTGEN_BUILD_SYSTEM" in os.environ

old_qemu_only = unittest.skipIf(VSSIM_NEXTGEN_BUILD_SYSTEM, reason="Old qemu only")
new_qemu_only = unittest.skipIf(not VSSIM_NEXTGEN_BUILD_SYSTEM, reason="New qemu only")

NVME_COMPLIANCE_TEST_DIR = "/home/esd/nvmeCompl/"
TEST_DIR = "/home/esd/guest/nvme_compliance_tests"

# Set to true and define DEBUG in sysdnvme.h to collect dnvme kernel log
# Note that having debug on considerably slows down testing, specifically suite 4
DEBUG = False


class TestNVMeCompliance:
    def test_NVMeCompliance(self):
        if 0 != subprocess.call("lsmod | grep dnvme",
                                shell=True, stdout=None, stderr=subprocess.STDOUT):
            # Each controller dnvme binds emits a PCI bind uevent carrying the
            # NVMe modalias, so udev autoloads nvme, which then takes any
            # controller dnvme has not bound yet. Blacklist nvme first.
            with open("/etc/modprobe.d/blacklist-nvme.conf", "w") as f:
                f.write("blacklist nvme\n")
            subprocess.check_call(["udevadm", "control", "--reload"])
            os.system("rmmod nvme")
            subprocess.check_call(["insmod", "dnvme.ko"])
            subprocess.check_call(["udevadm", "settle"])
            assert not os.path.exists("/sys/bus/pci/drivers/nvme"), \
                "nvme was reloaded and may own controllers dnvme needs"

        skip_single_tests = []
        with open("skipTests", "w+") as f:
            for test in skip_single_tests:
                f.write("%s\n" % test)

        skip_suits = {}
        if DEBUG:
            self.with_kernel_log(skip_suits)
        else:
            self.no_kernel_log(skip_suits)

    def with_kernel_log(self, skip_suits):
        for suiteNum in range(1, 28):
            if suiteNum not in skip_suits:
                cmd = "./tnvme --rev=1.2 --test=%d --skiptest=skipTests > ./Logs/test%d.txt 2>&1" % (suiteNum, suiteNum)
                dump_kernel = "./Logs/kdump%d.txt" % (suiteNum)
                print(cmd)
                dump_file = open(dump_kernel, 'w')
                proc = subprocess.Popen(["./log_kernel.sh"], stdout = dump_file)
                print("START %d" % int(time.time()))
                res = os.system(cmd)
                print("STOP %d" % int(time.time()))
                time.sleep(2)
                proc.kill()
                assert 0 == res, "Failed running suit %s" % suiteNum

    def no_kernel_log(self, skip_suits):
        for suiteNum in range(1, 28):
            if suiteNum not in skip_suits:
                cmd = "./tnvme --rev=1.2 --test=%d --skiptest=skipTests > ./Logs/test%d.txt 2>&1" % (suiteNum, suiteNum)
                print(cmd)
                print("START %d" % int(time.time()))
                res = os.system(cmd)
                print("STOP %d" % int(time.time()))
                assert 0 == res, "Failed running suit %s" % suiteNum
