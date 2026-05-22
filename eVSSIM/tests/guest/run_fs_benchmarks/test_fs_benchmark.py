#!/usr/bin/env python

import contextlib
import os
import subprocess
import tempfile
import unittest

MOUNT_POINT = "/mnt/exofs0"
YABS_DIR = os.environ.get("YABS_DIR", "/home/esd/yet-another-bench-script")
OUTPUT_DIR = os.environ.get("OUTPUT_DIR", "/tmp/output")
YABS_OUTPUT_DIR = os.path.join(OUTPUT_DIR, "yabs")
YABS_SCRIPT = os.path.join(YABS_DIR, "yabs.sh")


@contextlib.contextmanager
def output_stream(output_path=None):
    # type: (str) -> object
    if output_path is None:
        with tempfile.NamedTemporaryFile() as output_file:
            yield output_file
        return

    with open(output_path, "wb") as output_file:
        yield output_file


def run(command, cwd=None, output_path=None):
    # type: (list, str, str) -> int

    with output_stream(output_path) as output_file:
        return subprocess.call(command, cwd=cwd, stdout=output_file, stderr=subprocess.STDOUT)


def benchmark_enabled():
    # type: () -> bool
    return os.environ.get("EVSSIM_RUN_FS_BENCHMARKS", "yes").lower() not in ("0", "false", "n", "no")


class TestFsBenchmark(object):
    def test_run_yabs_disk_only_on_exofs(self):
        if not benchmark_enabled():
            raise unittest.SkipTest("Set EVSSIM_RUN_FS_BENCHMARKS=no to disable filesystem benchmarks")

        assert os.path.ismount(MOUNT_POINT), "%s is not mounted" % MOUNT_POINT
        assert os.path.isfile(YABS_SCRIPT), "%s is missing; provide a pre-provisioned YABS checkout via YABS_DIR" % YABS_SCRIPT

        if not os.path.isdir(YABS_OUTPUT_DIR):
            os.makedirs(YABS_OUTPUT_DIR)

        json_path = os.path.join(YABS_OUTPUT_DIR, "yabs.json")
        text_path = os.path.join(YABS_OUTPUT_DIR, "yabs.txt")
        assert 0 == run(["bash", YABS_SCRIPT, "-ign", "-w", json_path], cwd=MOUNT_POINT, output_path=text_path)
        assert os.path.isfile(json_path)
        assert os.path.isfile(text_path)
