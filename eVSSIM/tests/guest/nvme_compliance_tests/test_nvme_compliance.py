#!/usr/bin/env python

import os

NVME_COMPLIANCE_TEST_DIR = "/home/esd/nvmeCompl/"

class TestNVMeCompliance:
    def test_NVMeCompliance_pre(self):
        os.system("rmmod nvme")
        os.chdir(NVME_COMPLIANCE_TEST_DIR + "dnvme")
        assert 0 == os.system("make clean && make && insmod dnvme.ko")

    def test_tnvme(self):
        os.chdir(NVME_COMPLIANCE_TEST_DIR + "tnvme")
        assert 0 == os.system("make clean && make && ./run_tests.sh")
