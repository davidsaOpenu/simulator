#!/usr/bin/env python

import os
import unittest

FIO_TEST_DIR = "/home/esd/guest/fio_tests"

class TestFio:
    def test_fio_pre(self):
        os.system("rmmod dnvme")
        assert 0 == os.system("modprobe nvme")
        assert 0 == os.system("lsmod | grep -i nvme")
        assert 0 == os.system("apt-get -y install fio")
        # ./perf_test_*.sh assume that they are invoked from FIO_TEST_DIR
        os.chdir(FIO_TEST_DIR)
        assert 0 == os.system("mkdir -p reference_results")

    def test_run_short_mode_recording(self):
        assert 0 == os.system("./perf_test_rec_short.sh")

    def test_run_short_mode_comparison(self):
        assert 0 == os.system("./perf_test_run_short.sh")
    '''
    def test_run_recording(self):
        assert 0 == os.system("./perf_test_rec.sh")

    def test_run_comparison(self):
        assert 0 == os.system("./perf_test_run.sh")
    '''
    def test_run_surface_scan(self):
        assert 0 == os.system("fio ./fio_jobs/surface-scan.fio")

    @unittest.skip("Multi-disk test requires multiple controllers (nvme1, nvme2), not namespaces on single controller")
    def test_run_multi_disks(self):
        assert 0 == os.system("./perf_test_multi_disks.sh")
