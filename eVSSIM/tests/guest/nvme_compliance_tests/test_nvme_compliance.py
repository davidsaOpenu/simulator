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
    @old_qemu_only
    def test_NVMeCompliance_old(self):
        os.system("rmmod nvme")
        os.system("insmod " + NVME_COMPLIANCE_TEST_DIR + "/dnvme/dnvme.ko")

        os.chdir(NVME_COMPLIANCE_TEST_DIR + "tnvme")
        assert 0 == os.system("mkdir -p ./Logs")

        skipSingleTests = ['5:5.10.0', '16:1.0.0', '17:1.0.0']
        f= open("skipTests","w+")
        for test in skipSingleTests:
            f.write("%s\n" % test)
        f.close()

        skipEntireSuites = []
        for suiteNum in range(0, 24):
            if suiteNum not in skipEntireSuites:
                cmd = "./tnvme --test=%d --skiptest=skipTests > test%d.txt 2>&1" % (suiteNum, suiteNum)
                print cmd
                assert 0 == os.system(cmd)
    

    @new_qemu_only
    def test_NVMeCompliance_new(self):
        if 0 != subprocess.call("lsmod | grep dnvme",
                                shell=True, stdout=None, stderr=subprocess.STDOUT):
            os.system("rmmod nvme")
            os.system("insmod dnvme.ko")

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
                print cmd
                dump_file = open(dump_kernel, 'w')
                proc = subprocess.Popen(["./log_kernel.sh"], stdout = dump_file)
                print "START", int(time.time())
                res = os.system(cmd)
                print "STOP", int(time.time())
                time.sleep(2)
                proc.kill()
                assert 0 == res, "Failed running suit %s" % suiteNum
                
    def no_kernel_log(self, skip_suits):
        for suiteNum in range(1, 28):
            if suiteNum not in skip_suits:
                cmd = "./tnvme --rev=1.2 --test=%d --skiptest=skipTests > ./Logs/test%d.txt 2>&1" % (suiteNum, suiteNum)
                print cmd
                print "START", int(time.time())
                res = os.system(cmd)
                print "STOP", int(time.time())
                assert 0 == res, "Failed running suit %s" % suiteNum
