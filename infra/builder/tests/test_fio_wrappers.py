"""Run the production S6 wrappers/parser with a controlled fio child."""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


FIO = Path(__file__).resolve().parents[3] / "eVSSIM/tests/guest/fio_tests"
SPEC = importlib.util.spec_from_file_location("fio_metrics", FIO / "fio_metrics.py")
metrics = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(metrics)

FAKE_FIO = r'''#!/usr/bin/env python3
import os
from pathlib import Path
import sys
job = Path(sys.argv[1]).stem
with open(os.environ['FIO_CALLS'], 'a') as stream:
    stream.write(job + '\n')
number = len(Path(os.environ['FIO_CALLS']).read_text().splitlines())
directions = ['write', 'read'] if '_rw' in job else ['read'] if '_read' in job else ['write']
for direction in directions:
    if direction == os.environ.get('MISSING_DIRECTION') and number == int(os.environ.get('BAD_INVOCATION', 1)):
        continue
    print('  %s: io=80MB, bw=4096KB/s, iops=%s, runt=2000msec' %
          (direction, os.environ.get('FAKE_IOPS', '100')))
print('child stderr invocation %d' % number, file=sys.stderr)
if number == int(os.environ.get('FAIL_INVOCATION', 0)):
    sys.exit(7)
'''


class FioMetricsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def output(self, text):
        path = self.root / "output"
        path.write_text(text)
        return str(path)

    def test_old_and_new_directional_units(self):
        for text, expected in [
            (" read : io=1MB, bw=1MB/s, iops=12, runt=1000msec", 12),
            ("READ: bw=1MiB/s, IOPS=12.5k, io=1MiB", 12500),
            ("read: IOPS=2M, BW=1MiB/s", 2000000),
            ("read: IOPS=1.25G, BW=1MiB/s", 1250000000),
        ]:
            with self.subTest(text=text):
                self.assertEqual(metrics.iops(self.output(text), ["read"]), {"read": expected})

    def test_bad_missing_duplicate_or_error_metrics_fail(self):
        for text in ["", "read: IOPS=0", "read: IOPS=-2", "read: IOPS=nan",
                     "read: IOPS=12junk", "read: IOPS=1.2.3", "read: IOPS=1e3",
                     "read: IOPS=4\nread: IOPS=5", "write: IOPS=3",
                     "read: IOPS=10\njob: err=5"]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                metrics.iops(self.output(text), ["read"])

    def test_required_directions_come_from_real_jobs(self):
        self.assertEqual(metrics.directions(str(FIO / "fio_jobs/test_rw_short.fio")), ["write", "read"])
        self.assertEqual(metrics.directions(str(FIO / "fio_jobs/test_read_short.fio")), ["read"])
        self.assertEqual(metrics.directions(str(FIO / "fio_jobs/test_write_short.fio")), ["write"])


class FioWrapperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copytree(FIO, self.root / "fio")
        self.work = self.root / "fio"
        (self.work / "reference_results").mkdir(exist_ok=True)
        binary = self.root / "bin"
        binary.mkdir()
        (binary / "fio").write_text(FAKE_FIO)
        (binary / "fio").chmod(0o755)
        # Reject the old interpreter name so it cannot mask a Python 3 regression.
        (binary / "python").write_text("#!/bin/sh\necho 'use python3' >&2\nexit 99\n")
        (binary / "python").chmod(0o755)
        (binary / "python3").write_text("#!/bin/sh\nexec " + shlex.quote(sys.executable) + ' "$@"\n')
        (binary / "python3").chmod(0o755)
        self.calls = self.root / "calls"
        # Keep wrapper results in the fixture instead of touching shared host files.
        self.results = self.root / "results"
        self.results.mkdir()
        self.env = dict(os.environ, PATH=str(binary) + os.pathsep + os.environ["PATH"],
                        FIO_CALLS=str(self.calls), TMPDIR=str(self.results))
        self.raw_results = []
        for job in (self.work / "fio_jobs").glob("test_*.fio"):
            directions = metrics.directions(str(job))
            self.reference(job.stem, "\n".join(d + ": IOPS=100" for d in directions))

    def reference(self, job, text):
        path = self.work / "reference_results" / (job + "_ref_res")
        path.write_text(text)
        return path

    def run_wrapper(self, script, *args, **env):
        result = subprocess.run(["bash", "./" + script] + list(args), cwd=self.work,
                                env=dict(self.env, **env), text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for raw in re.findall(r"raw result: ([^\n]+)", result.stdout):
            if Path(raw).is_absolute():
                self.raw_results.append(Path(raw))
        return result

    def test_outputs_use_selected_temp_directory(self):
        selected = self.root / "results with spaces"
        selected.mkdir()
        result = self.run_wrapper("perf_test.sh", "test_rw_short", TMPDIR=str(selected))
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(len(self.raw_results), 2)
        for raw in self.raw_results:
            self.assertEqual(raw.parent, selected)
            self.assertTrue(raw.is_file())
        self.assertEqual((selected / "test_rw_short_res").read_bytes(),
                         self.raw_results[-1].read_bytes())
        self.assertEqual(list(self.results.iterdir()), [])

    def test_valid_higher_lower_and_equal_are_non_gating(self):
        for current, message in [("105", "5% higher"), ("90", "10% lower"), ("100", "0% unchanged")]:
            with self.subTest(current=current):
                result = self.run_wrapper("perf_test.sh", "test_read_short", FAKE_IOPS=current)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertIn(message, result.stdout)

    def test_mixed_runs_both_children_and_preserves_distinct_outputs(self):
        result = self.run_wrapper("perf_test.sh", "test_rw_short")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(self.calls.read_text().splitlines(), ["test_rw_short"] * 2)
        self.assertEqual(len(self.raw_results), 2)
        for number, path in enumerate(self.raw_results, 1):
            self.assertIn("child stderr invocation %d" % number, path.read_text())

    def test_missing_mixed_direction_fails_even_on_other_comparison(self):
        for missing, invocation in [("read", "1"), ("write", "2")]:
            with self.subTest(missing=missing):
                self.calls.unlink(missing_ok=True)
                result = self.run_wrapper("perf_test.sh", "test_rw_short",
                                          MISSING_DIRECTION=missing, BAD_INVOCATION=invocation)
                self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_missing_or_malformed_reference_does_not_skip_coverage(self):
        reference = self.work / "reference_results/test_rw_short_ref_res"
        for text in [None, "write: IOPS=100", "write: IOPS=0\nread: IOPS=100"]:
            with self.subTest(text=text):
                if text is None:
                    reference.unlink()
                else:
                    reference.write_text(text)
                result = self.run_wrapper("perf_test.sh", "test_rw_short")
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertFalse(self.calls.exists())

    def test_compare_and_record_propagate_child_status_and_retain_raw(self):
        for script in ("perf_test.sh", "perf_test_rec_results.sh"):
            with self.subTest(script=script):
                self.calls.unlink(missing_ok=True)
                reference = self.work / "reference_results/test_write_short_ref_res"
                before = reference.read_bytes()
                result = self.run_wrapper(script, "test_write_short", FAIL_INVOCATION="1")
                self.assertEqual(result.returncode, 7, result.stdout)
                self.assertEqual(reference.read_bytes(), before)
                paths = re.findall(r"raw result: (\S+)", result.stdout)
                self.assertEqual(len(paths), 1)
                raw = Path(paths[0])
                if not raw.is_absolute():
                    raw = self.work / raw
                self.assertIn("child stderr invocation 1", raw.read_text())

    def test_record_does_not_publish_incomplete_mixed_measurement(self):
        reference = self.work / "reference_results/test_rw_short_ref_res"
        before = reference.read_bytes()
        result = self.run_wrapper("perf_test_rec_results.sh", "test_rw_short", MISSING_DIRECTION="read")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertEqual(reference.read_bytes(), before)

    def test_new_reference_is_readable_by_guest_evidence_collector(self):
        reference = self.work / "reference_results/test_write_short_ref_res"
        reference.unlink()
        result = self.run_wrapper("perf_test_rec_results.sh", "test_write_short")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(reference.stat().st_mode & 0o044, 0o044)

    def test_all_four_batch_wrappers_retain_failure_after_later_successes(self):
        for script in ("perf_test_rec_short.sh", "perf_test_run_short.sh", "perf_test_rec.sh", "perf_test_run.sh"):
            with self.subTest(script=script):
                self.calls.unlink(missing_ok=True)
                result = self.run_wrapper(script, FAIL_INVOCATION="1")
                self.assertEqual(result.returncode, 7, result.stdout)
                expected = ["test_write", "test_rw", "test_read"]
                if "run" in script:
                    expected.insert(2, "test_rw")
                if "short" in script:
                    expected = [job + "_short" for job in expected]
                self.assertEqual(self.calls.read_text().splitlines(), expected)
                self.assertNotIn("ALL DONE", result.stdout)

    def test_successful_short_batches_keep_original_workload_order(self):
        for script in ("perf_test_rec_short.sh", "perf_test_run_short.sh"):
            with self.subTest(script=script):
                self.calls.unlink(missing_ok=True)
                result = self.run_wrapper(script)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertIn("ALL DONE", result.stdout)


if __name__ == "__main__":
    unittest.main()
