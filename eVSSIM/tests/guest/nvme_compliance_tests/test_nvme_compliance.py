#!/usr/bin/env python

import os
import pytest
import subprocess

VSSIM_NEXTGEN_BUILD_SYSTEM = "VSSIM_NEXTGEN_BUILD_SYSTEM" in os.environ

old_qemu_only = pytest.mark.skipif(VSSIM_NEXTGEN_BUILD_SYSTEM, reason="Old qemu only")
new_qemu_only = pytest.mark.skipif(not VSSIM_NEXTGEN_BUILD_SYSTEM, reason="New qemu only")

NVME_COMPLIANCE_TEST_DIR = "/home/esd/nvmeCompl/"
TEST_DIR = "/home/esd/guest/nvme_compliance_tests"

class TestNVMeCompliance:
    @old_qemu_only
    def test_NVMeCompliance_old(self):
        os.system("rmmod nvme")

        os.chdir(NVME_COMPLIANCE_TEST_DIR + "dnvme")
        os.system("insmod dnvme.ko")

        os.chdir(NVME_COMPLIANCE_TEST_DIR + "tnvme")
        assert 0 == os.system("mkdir -p ./Logs")
        skipSuites = [ 16, 17]
        for suiteNum in range(0, 24):
            if suiteNum not in skipSuites:
                cmd = "./tnvme --test=%d > test%d.txt 2>&1" % (suiteNum, suiteNum)
                print cmd
                assert 0 == os.system(cmd)

    @new_qemu_only
    def test_NVMeCompliance_new(self):
        if 0 != subprocess.call("lsmod | grep dnvme",
                                shell=True, stdout=None, stderr=subprocess.STDOUT):
            os.system("rmmod nvme")
            os.system("insmod dnvme.ko")

        skip_suits = {19, 22, 23, 24}
        for suiteNum in range(1, 28):
            if suiteNum not in skip_suits:
                cmd = "./tnvme --rev=1.2 --test=%d > ./Logs/test%d.txt 2>&1" % (suiteNum, suiteNum)
                print cmd
                assert 0 == os.system(cmd), "Failed running suit %s" % suiteNum
