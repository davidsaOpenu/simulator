#!/usr/bin/env python

import os

NVME_COMPLIANCE_TEST_DIR = "/home/esd/nvmeCompl/"
TEST_DIR = "/home/esd/guest/nvme_compliance_tests"

class TestNVMeCompliance:
    def test_NVMeCompliance_pre(self):
        if (0 == os.system(TEST_DIR +"/linux_module_status.sh nvme")):
            assert 0 == os.system("rmmod nvme")
        os.chdir(NVME_COMPLIANCE_TEST_DIR + "dnvme")
        assert 0 == os.system("insmod dnvme.ko")

    def test_tnvme(self):
        os.chdir(NVME_COMPLIANCE_TEST_DIR + "tnvme")
        assert 0 == os.system("./run_tests.sh")

