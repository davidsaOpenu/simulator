#!/usr/bin/env python
import os

# Config #3 kernel branch - matches configs.json config id 3
EXPECTED_KERNEL_BRANCH = "7.0.2"

class TestKernelVersion:
    def test_kernel_version(self):
        uname = os.popen("uname -a").read()
        print("uname -a: {}".format(uname))
        assert EXPECTED_KERNEL_BRANCH in uname, \
            "FAIL: Expected kernel {} not found in: {}".format(EXPECTED_KERNEL_BRANCH, uname)
