#!/usr/bin/env python

import os

NVME_COMPLIANCE_TEST_DIR = "/home/esd/nvmeCompl/"
TEST_DIR = "/home/esd/guest/nvme_compliance_tests"

EVSSIM_NEW_BUILDER_GUEST = "EVSSIM_NEW_BUILDER_GUEST" in os.environ

class TestNVMeCompliance:
    def test_NVMeCompliance(self):
        os.system("ls -al .")
        os.system("rmmod nvme")

        if not EVSSIM_NEW_BUILDER_GUEST:
            os.chdir(NVME_COMPLIANCE_TEST_DIR + "dnvme")
        os.system("insmod dnvme.ko")

        if not EVSSIM_NEW_BUILDER_GUEST:
            os.chdir(NVME_COMPLIANCE_TEST_DIR + "tnvme")
        assert 0 == os.system("mkdir -p ./Logs")
        skipSuites = [ 16, 17]
        for suiteNum in range(0, 24):
            if suiteNum not in skipSuites:
               cmd = "./tnvme --test=%d > test%d.txt 2>&1" % (suiteNum, suiteNum)
               print cmd
               assert 0 == os.system(cmd)
