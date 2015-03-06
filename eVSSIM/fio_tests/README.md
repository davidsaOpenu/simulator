# VSSIM fio tests
=================

contains various tests for storage performance and integrity

## Important note before using this
all tests writing directly to block device /dev/sdb. Not a good thing to enable by default, so its off.
to enable for all tests, use enable_sdb.sh. To disable, use disable_sdb.sh.

## Short story:
 * use `surface-scan.fio` to run a surface test
 * use `perf_test_rec_short.sh` to prepare reference results for the short tests.
 * use `perf_test_run_short.sh` to run the short test (againgst the pre-made prepare reference).
 * use `perf_test_rec.sh` to prepare reference results for the long tests.
 * use `perf_test_run.sh` to run the long test (againgst the pre-made prepare reference).

## Long story:

### Surface scan
`surface-scan.fio` can be used to run surface scan on all the device bits. it writes and read. 
to simulate fail, run `surface-scan-fail.fio` (which write different bits) while `surface-scan.fio` is running.

### Performance tests
idea is to run a test, store its result and re-run it.

 1. running and recording a referebce result is done using `perf_test_rec.sh foo`. it uses a `corresping fio_jobs/test_foo.fio` job.
 1. re-running the test and validating results against the pre-made is done with `perf_test.sh foo`
 1. there are 3 jobs - read, write, and rw (half read half write). for each there two versions - the full one (e.g. `fio_jobs/test_read.fio`) and a short one for sanity (e.g. `fio_jobs/test_read_short.fio`). 
 1. there are helper scripts which simply uses the above jobs sequentially (`perf_test_rec_short.sh`, `perf_test_run_short.sh`, `perf_test_rec.sh`, `perf_test_run.sh`)

