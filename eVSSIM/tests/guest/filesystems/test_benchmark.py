#!/usr/bin/env python

from __future__ import print_function

import json
import os
import subprocess

from fs_common import (assert_all_devices_present, find_filesystem,
                       load_nvme_driver, run_fs_lib)

FIO_JOB = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "fs_benchmark.fio")
OUTPUT_DIR = "/tmp/benchmark"
EXT4_LIB = "/home/esd/ext4/ext4_lib.sh"


def _benchmark_target(name):
    # type: (str) -> dict
    fs = dict(find_filesystem(name))
    fs["setup"] = "setup_" + name
    fs["teardown"] = "teardown_" + name
    if name == "exofs":
        # TODO Replace this ext4 stand-in with exofs (setup_exofs /
        # teardown_exofs from exofs_lib.sh) once exofs writes file data
        fs["lib"] = EXT4_LIB
        fs["setup"] = ("/home/esd/guest/nvme set-feature %s -f 0xc0 --value=0"
                       " && setup_ext4" % fs["dev"])
        fs["teardown"] = "teardown_ext4"
    return fs


def _run_fio(fs, json_path):
    # type: (dict, str) -> None
    cmd = ["fio", "--directory=" + fs["mountpoint"], "--output-format=json",
           FIO_JOB]
    print("[%s] $ %s" % (fs["dev"], " ".join(cmd)))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = proc.communicate()
    if not isinstance(stdout, str):
        stdout = stdout.decode("utf-8", "replace")
        stderr = stderr.decode("utf-8", "replace")
    if stderr.strip():
        print(stderr.rstrip())

    lines = stdout.splitlines(True)
    start = 0
    while start < len(lines) and not lines[start].startswith("{"):
        print(lines[start].rstrip())
        start += 1
    report = "".join(lines[start:])
    assert proc.returncode == 0, (
        "fio failed on %s with exit code %d" % (fs["fs"], proc.returncode))

    try:
        jobs = json.loads(report)["jobs"]
    except (ValueError, KeyError) as error:
        raise AssertionError("fio wrote no usable JSON on %s: %s"
                             % (fs["fs"], error))
    failed = [job["jobname"] for job in jobs if job["error"]]
    assert not failed, ("fio jobs failed on %s: %s"
                        % (fs["fs"], ", ".join(failed)))

    handle = open(json_path, "w")
    try:
        handle.write(report)
    finally:
        handle.close()


class TestFilesystemBenchmark:

    def setup(self):
        # type: () -> None
        load_nvme_driver()
        assert_all_devices_present()

    def _benchmark(self, name):
        # type: (str) -> None
        fs = _benchmark_target(name)
        if not os.path.isdir(OUTPUT_DIR):
            os.makedirs(OUTPUT_DIR)
        json_path = os.path.join(OUTPUT_DIR, "fio-%s.json" % name)
        if os.path.exists(json_path):
            os.remove(json_path)

        try:
            run_fs_lib(fs, fs["setup"])
            _run_fio(fs, json_path)
        finally:
            rc, _output = run_fs_lib(fs, fs["teardown"], check=False)
        assert rc == 0, (
            "%s failed on %s with exit code %d; %s may still be mounted or busy"
            % (fs["teardown"], fs["dev"], rc, fs["mountpoint"]))
        print("fio results for %s at %s" % (name, json_path))

    def test_benchmark_ext4(self):
        # type: () -> None
        self._benchmark("ext4")

    def test_benchmark_exofs(self):
        # type: () -> None
        self._benchmark("exofs")
