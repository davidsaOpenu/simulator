#!/usr/bin/env python
import os
import json

CONFIG_FILE = "/etc/evssim/configs.json"
CONFIG_ID_FILE = "/etc/evssim/config_id"
KERNEL_VERSION_FILE = "/etc/evssim/kernel_version"

class TestKernelVersion:
    def test_kernel_version(self):
        # Read current config ID
        with open(CONFIG_ID_FILE) as f:
            config_id = int(f.read().strip())

        # Read expected kernel version from configs.json
        with open(CONFIG_FILE) as f:
            configs = json.load(f)
        config = next(c for c in configs["configs"] if c["id"] == config_id)
        config_branch = config["kernel"]["branch"]

        # Read actual compiled kernel version
        with open(KERNEL_VERSION_FILE) as f:
            expected_version = f.read().strip()

        # Run uname -a and check kernel version
        uname = os.popen("uname -a").read()
        print("uname -a: {}".format(uname))
        print("Config #{} kernel branch: {}".format(config_id, config_branch))
        print("Expected kernel version: {}".format(expected_version))
        assert expected_version in uname, \
            "FAIL: Expected kernel {} not found in: {}".format(expected_version, uname)
