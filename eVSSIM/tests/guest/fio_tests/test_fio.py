#!/usr/bin/env python


import os

GUEST_TEST_DIR = "/home/esd/guest/"

class TestFio:
    def test_fio_pre(self):
        os.system("rmmod dnvme")
        assert 0 == os.system("modprobe nvme")
        assert 0 == os.system("lsmod | grep -i nvme")
        assert 0 == os.system("rm -rf fio_tests")
        assert 0 == os.system("apt-get -y install fio")
        assert 0 == os.system("mkdir -p reference_results")

    def test_run_short_mode_recording(self):
        assert 0 == os.system(GUEST_TEST_DIR + "/fio_tests/perf_test_rec_short.sh")


    def test_run_short_mode_comparison(self):
        assert 0 == os.system(GUEST_TEST_DIR + "/fio_tests/perf_test_run_short.sh")

    def test_run_recording(self):
        assert 0 == os.system(GUEST_TEST_DIR + "/fio_tests/perf_test_rec.sh")

    def test_run_comparison(self):
        assert 0 == os.system(GUEST_TEST_DIR + "/fio_tests/perf_test_run.sh")

    def test_run_surface_scan(self):
        assert 0 == os.system("fio " + GUEST_TEST_DIR + "fio_tests/fio_jobs/surface-scan.fio")


