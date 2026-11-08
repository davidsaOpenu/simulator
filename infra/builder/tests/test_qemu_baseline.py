"""Exercise S5 result accounting with real runners and simulated guest boundaries."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import runpy
import shutil
import signal
import subprocess
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET


BUILDER = Path(__file__).resolve().parents[1]
SIMULATOR = BUILDER.parent.parent
GUEST = SIMULATOR / "eVSSIM/tests/guest"
SPEC = importlib.util.spec_from_file_location("qemu_baseline", BUILDER / "collect-qemu11-baseline.py")
baseline = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(baseline)

# These boundary fixtures do not implement the collector's loop or decisions.
# They supply observed command outcomes; the production shell/Python decides
# continuation, recovery, identity, completeness and pass/fail.
BUILDER_FIXTURE = r'''
set -eu -o pipefail
export EVSSIM_ROOT_PATH=$BASELINE_FIXTURE
export EVSSIM_BUILDER_FOLDER=builder
export EVSSIM_QEMU_COMPILE_CONTAINER=ubuntu-26.04
export EVSSIM_VERSIONS_FOLDER=versions
export EVSSIM_GUEST_HOME_PATH=/home/esd
EVSSIM_DOCKER_UUID=""
EVSSIM_PROVISIONED_DEVICE_COUNT=6
launches=0
record_event() { printf '%s\n' "$*" >> "$BASELINE_FIXTURE/events"; }
evssim_qemu_fresh_image() {
    # Docker -i may consume stdin. The workload list must use its own FD.
    cat >/dev/null
    record_event "fresh $group"
    if [ "$BASELINE_MODE" = setup-failure ] && [ "$group" = fio ]; then return 23; fi
    if [ "$BASELINE_MODE" = recovery-failure ] && [ "$group" = compliance ] && [ "$launches" = 1 ]; then return 24; fi
}
evssim_qemu_detached() {
    launches=$((launches + 1))
    EVSSIM_DOCKER_UUID=container-$group-$launches
    EVSSIM_QEMU_LAUNCH_SELECTION=$(cat "$BASELINE_FIXTURE/selection.json")
    export BASELINE_GROUP=$group
    record_event "launch $group $launches $EVSSIM_RUNTIME_STORAGE_STRATEGY $EVSSIM_QEMU_SIMULATOR_ENABLED"
    trap 'evssim_qemu_stop' EXIT
}
evssim_qemu_stop() {
    record_event "stop $group $EVSSIM_DOCKER_UUID"
    EVSSIM_DOCKER_UUID=""
    rm -f "$BASELINE_FIXTURE/dead"
    if [ "$BASELINE_MODE" = stop-failure ]; then return 12; fi
}
evssim_qemu_flush_disk() {
    record_event "flush $group"
    if [ "$BASELINE_MODE" = flush-failure ]; then return 13; fi
}
evssim_guest_probe() { return 0; }
evssim_qemu_is_running() { [ ! -f "$BASELINE_FIXTURE/dead" ]; }
evssim_copy_from_guest() {
    record_event "copy $1"
    if [ "$BASELINE_MODE" = copy-failure ]; then return 14; fi
    local name=${1##*/}
    if [ "$name" = qemu11-baseline ]; then
        cp -r "$BASELINE_FIXTURE/guest" "$2/qemu11-baseline"
    else cp -r "$BASELINE_FIXTURE/guest/$name" "$2"; fi
}
evssim_guest() {
    record_event "guest $*"
    echo "guest output: $*"
    if [[ "$*" == *'--test=4 '* ]]; then
        case "$BASELINE_MODE" in
            suite-failure) return 7 ;;
            suite-ssh-error) touch "$BASELINE_FIXTURE/dead"; return 255 ;;
            recovery-failure) touch "$BASELINE_FIXTURE/dead"; return 8 ;;
        esac
    fi
    if [[ "$*" == *'--xunit-file='* ]]; then
        python3 "$BASELINE_FIXTURE/guest-output.py" "$group"
    elif [[ "$*" == *'qemu11-baseline-fio.sh'* ]]; then
        local name=${1#*/home/esd/guest/Logs/qemu11-baseline/}
        name=${name%% *}
        python3 "$BASELINE_FIXTURE/guest-output.py" fio "$name"
        if [ "$BASELINE_MODE" = workload-ssh-error ] && [ "$name" = compare-write ]; then
            return 255
        fi
        if [ "$BASELINE_MODE" = fio-recovery ] && [ "$name" = compare-write ] && [ "$launches" = 1 ]; then
            touch "$BASELINE_FIXTURE/dead"; return 9
        fi
    fi
}
'''

GUEST_OUTPUT_FIXTURE = r'''
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as E
root = Path(os.environ["BASELINE_FIXTURE"])
group = sys.argv[1]
if group != "fio":
    directory = root / "guest" / group
    directory.mkdir(exist_ok=True)
    xml = E.Element("testsuite")
    if group == "ext4":
        E.SubElement(xml, "testcase", classname="ext4.TestExt4Format", name="test_format_all", time="1.25")
    else:
        for cls in ("test_NvmeCli", "test_NvmeCliMultiDisk"):
            for method in ("test_delete", "test_empty_list", "test_empty_object", "test_write_read_and_compare",
                           "test_max_size_object", "test_overwrite"):
                E.SubElement(xml, "testcase", classname="test_nvme_cli." + cls, name=method, time="2.5")
    E.ElementTree(xml).write(str(directory / "results.xml"))
else:
    name = sys.argv[2]
    directory = root / "guest" / name
    directory.mkdir(exist_ok=True)
    count = 2 if name == "compare-rw" else 1
    (directory / "wrapper-exit-status.txt").write_text("0\n")
    (directory / "reference.log").write_text(" read: IOPS=12k, BW=1M\n write: IOPS=800, BW=1M\n")
    for index in range(count):
        invocation = directory / ("invocation-" + str(index))
        invocation.mkdir(exist_ok=True)
        files = {"command.txt": "fio " + name, "started.txt": str(index), "elapsed-seconds.txt": "2",
                 "stdout.log": " read: IOPS=12.5k, BW=1M\n write: IOPS=900, BW=1M\n",
                 "stderr.log": "", "exit-status.txt": "0", "jobs.sha256": "recorded-job-hash",
                 "origin.txt": "wrapper"}
        for filename, text in files.items():
            (invocation / filename).write_text(text)
'''


class CollectorTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="qemu-baseline-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.builder = self.root / "builder"
        self.builder.mkdir()
        (self.root / "guest").mkdir()
        self.selection = {"status": "selected", "profile_id": 4, "key": "frozen-artifact"}
        baseline.write_json(self.root / "selection.json", self.selection)
        (self.builder / "builder.sh").write_text(BUILDER_FIXTURE)
        (self.root / "guest-output.py").write_text(GUEST_OUTPUT_FIXTURE)
        (self.builder / "qemu-artifact.py").write_text('''import json, os
from pathlib import Path
import sys
root = Path(os.environ["BASELINE_FIXTURE"])
selection = json.loads((root / "selection.json").read_text())
mode = os.environ["BASELINE_MODE"]
if mode == "reject-identity" and os.environ["BASELINE_GROUP"] == "compliance":
    sys.exit(3)
if mode == "wrong-identity":
    selection["key"] = "another-artifact"
print(json.dumps(selection))
''')
        self.environment = mock.patch.dict(os.environ, {
            "BASELINE_FIXTURE": str(self.root), "BASELINE_MODE": "success"})
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.run_dir = self.root / "evidence"
        self.run_dir.mkdir()

    def collect(self, mode="success"):
        os.environ["BASELINE_MODE"] = mode
        status = baseline.collect_baseline(self.builder, self.run_dir, self.selection, 60)
        return status, json.loads((self.run_dir / "results.json").read_text())

    def events(self):
        return (self.root / "events").read_text().splitlines()

    def test_all_pass_preserves_inventory_modes_identity_and_cleanup(self):
        status, report = self.collect()
        self.assertEqual(0, status)
        self.assertTrue(report["baseline_complete"])
        self.assertTrue(report["baseline_passed"])
        self.assertEqual([], report["pending_groups"])
        compliance = report["groups"]["compliance"]
        self.assertEqual(list(range(1, 28)), [r["suite"] for r in compliance["results"]])
        for result in compliance["results"]:
            self.assertIn("--rev=1.2", result["command"])
            self.assertIn("--skiptest=skipTests", result["command"])
            self.assertEqual(0, result["exit_status"])
            self.assertGreaterEqual(result["elapsed_seconds"], 0)
            self.assertTrue((self.run_dir / result["raw_output"]).is_file())
            runtime = json.loads((self.run_dir / result["runtime_identity"]).read_text())
            self.assertEqual("frozen-artifact", runtime["key"])
        self.assertEqual(["record-write", "record-rw", "record-read", "compare-write", "compare-rw",
                          "compare-read", "surface", "multi"],
                         [r["case"] for r in report["groups"]["fio"]["results"]])
        objects = report["groups"]["objects_via_ioctl"]["results"][0]["observations"]["cases"]
        self.assertEqual(8, sum(c["status"] == "passed" for c in objects))
        self.assertEqual(4, sum(c["status"] == "excluded" for c in objects))
        for launch in ("launch compliance 1 1 yes", "launch fio 1 1 no", "launch ext4 1 1 yes",
                       "launch objects_via_ioctl 1 2 yes"):
            self.assertIn(launch, self.events())
        for group in ("compliance", "fio", "ext4", "objects_via_ioctl"):
            self.assertIn("flush " + group, self.events())
            self.assertTrue(any(e.startswith("stop " + group) for e in self.events()))
        self.assertTrue(any("--prepare-only" in e for e in self.events()))

    def test_suite_four_failure_does_not_hide_later_suites_or_groups(self):
        status, report = self.collect("suite-failure")
        self.assertEqual(1, status)
        self.assertTrue(report["baseline_complete"])
        self.assertFalse(report["baseline_passed"])
        results = report["groups"]["compliance"]["results"]
        self.assertEqual(7, results[3]["exit_status"])
        self.assertEqual(0, results[26]["exit_status"])
        for group in ("fio", "ext4", "objects_via_ioctl"):
            self.assertTrue(report["groups"][group]["passed"])

    def test_setup_failure_blocks_only_its_group_and_still_cleans_up(self):
        status, report = self.collect("setup-failure")
        self.assertEqual(1, status)
        self.assertFalse(report["baseline_complete"])
        self.assertTrue(all(r["status"] == "blocked" for r in report["groups"]["fio"]["results"]))
        self.assertTrue(report["groups"]["ext4"]["passed"])
        self.assertTrue(report["groups"]["objects_via_ioctl"]["passed"])
        self.assertTrue(report["groups"]["fio"]["cleanup"][0]["recorded"])

    def test_failed_compliance_recovery_is_not_reported_as_executed(self):
        status, report = self.collect("recovery-failure")
        self.assertEqual(1, status)
        results = report["groups"]["compliance"]["results"]
        self.assertEqual(8, results[3]["exit_status"])
        self.assertTrue(all(r["status"] == "blocked" and r["exit_status"] is None for r in results[4:]))
        self.assertFalse(report["baseline_complete"])
        self.assertTrue(report["groups"]["fio"]["passed"])

    def test_suite_ssh_error_is_incomplete_and_later_suites_continue_after_recovery(self):
        status, report = self.collect("suite-ssh-error")
        self.assertEqual(1, status)
        self.assertFalse(report["baseline_complete"])
        compliance = report["groups"]["compliance"]
        self.assertFalse(compliance["compliance_complete"])
        suite = compliance["results"][3]
        self.assertEqual("incomplete", suite["status"])
        self.assertEqual(255, suite["exit_status"])
        self.assertIn("unconfirmed", suite["reason"])
        for key in ("raw_output", "runtime_identity", "guest_identity"):
            self.assertTrue((self.run_dir / suite[key]).is_file())
        self.assertEqual(1, sum("--test=4 " in e for e in self.events()))
        self.assertEqual(2, compliance["results"][4]["attempt"])
        self.assertTrue(all(r["status"] == "executed" for r in compliance["results"][4:]))
        self.assertTrue(compliance["cleanup"][0]["recorded"])
        self.assertTrue(compliance["cleanup"][1]["recorded"])
        self.assertIn("stop compliance container-compliance-2", self.events())
        self.assertTrue(report["groups"]["objects_via_ioctl"]["passed"])

    def test_workload_ssh_error_is_incomplete_even_with_collected_child_output(self):
        status, report = self.collect("workload-ssh-error")
        self.assertEqual(1, status)
        self.assertFalse(report["baseline_complete"])
        fio = report["groups"]["fio"]
        self.assertFalse(fio["complete"])
        case = fio["results"][3]
        self.assertEqual("compare-write", case["case"])
        self.assertEqual("incomplete", case["status"])
        self.assertEqual(255, case["exit_status"])
        self.assertEqual(0, case["collection_exit_status"])
        self.assertIn("unconfirmed", case["reason"])
        self.assertTrue((self.run_dir / case["raw_output"]).is_file())
        self.assertTrue(fio["results"][4]["passed"])
        self.assertTrue(fio["cleanup_passed"])
        self.assertTrue(report["groups"]["objects_via_ioctl"]["passed"])

    def test_fio_recovery_repeats_setup_references_and_same_artifact(self):
        status, report = self.collect("fio-recovery")
        self.assertEqual(1, status)
        self.assertTrue(report["baseline_complete"])
        self.assertEqual(2, report["groups"]["fio"]["results"][4]["attempt"])
        events = self.events()
        refresh = [e for e in events if e.startswith("guest ") and "/refresh-" in e]
        self.assertEqual(3, len(refresh))
        self.assertIn("refresh-write", refresh[0])
        self.assertIn("refresh-rw", refresh[1])
        self.assertIn("refresh-read", refresh[2])
        self.assertEqual(2, sum("test_fio_pre()" in e for e in events))
        self.assertLess(events.index(refresh[2]), next(i for i, e in enumerate(events) if
                        e.startswith("guest ") and "/compare-rw " in e))
        for result in report["groups"]["fio"]["results"]:
            self.assertEqual("frozen-artifact", json.loads(
                (self.run_dir / result["runtime_identity"]).read_text())["key"])

    def test_rejected_runtime_identity_blocks_compliance_but_not_later_groups(self):
        status, report = self.collect("reject-identity")
        self.assertEqual(1, status)
        self.assertTrue(all(r["status"] == "blocked" for r in report["groups"]["compliance"]["results"]))
        self.assertTrue(report["groups"]["objects_via_ioctl"]["passed"])

    def test_mismatched_recorded_identity_cannot_pass(self):
        status, report = self.collect("wrong-identity")
        self.assertEqual(1, status)
        self.assertFalse(report["baseline_complete"])
        self.assertTrue(all(r["status"] == "incomplete" for r in report["groups"]["compliance"]["results"]))
        self.assertFalse(report["groups"]["fio"]["passed"])

    def test_collection_failure_is_incomplete_evidence(self):
        status, report = self.collect("copy-failure")
        self.assertEqual(1, status)
        self.assertFalse(report["baseline_complete"])
        self.assertEqual(14, report["groups"]["fio"]["results"][0]["collection_exit_status"])
        self.assertTrue(any(e.startswith("stop objects_via_ioctl") for e in self.events()))

    def test_flush_or_stop_failure_cannot_turn_all_passing_tests_green(self):
        for mode in ("flush-failure", "stop-failure"):
            with self.subTest(mode=mode):
                os.environ["BASELINE_MODE"] = mode
                directory = self.root / mode
                directory.mkdir()
                status = baseline.collect_compliance(self.builder, directory, self.selection)
                report = json.loads((directory / "results.json").read_text())
                self.assertEqual(1, status)
                self.assertTrue(report["compliance_complete"])
                self.assertFalse(report["cleanup_passed"])
                self.assertTrue(any(e.startswith("stop compliance") for e in self.events()))

    def test_missing_output_and_result_never_count_as_complete(self):
        baseline.collect_compliance(self.builder, self.run_dir, self.selection)
        (self.run_dir / "compliance/suite-3/output.log").unlink()
        (self.run_dir / "compliance/suite-9/result.tsv").unlink()
        results = baseline.compliance_results(self.run_dir)
        self.assertEqual("incomplete", results[2]["status"])
        self.assertEqual("blocked", results[8]["status"])
        self.assertIsNone(results[8]["exit_status"])

    def test_truncated_suite_record_does_not_erase_other_suite_results(self):
        baseline.collect_compliance(self.builder, self.run_dir, self.selection)
        (self.run_dir / "compliance/suite-4/result.tsv").write_text("executed\t0")
        results = baseline.compliance_results(self.run_dir)
        self.assertEqual(27, len(results))
        self.assertEqual("incomplete", results[3]["status"])
        self.assertEqual("executed", results[26]["status"])
        self.assertEqual(0, results[26]["exit_status"])

    def test_interrupt_signals_runner_session_and_writes_incomplete_report(self):
        process = mock.Mock(pid=12345)
        process.wait.side_effect = [KeyboardInterrupt, 143]
        process.poll.return_value = None
        with mock.patch.object(baseline.subprocess, "Popen", return_value=process), \
                mock.patch.object(baseline.os, "killpg") as kill, self.assertRaises(KeyboardInterrupt):
            baseline.collect_compliance(self.builder, self.run_dir, self.selection)
        kill.assert_called_once_with(12345, signal.SIGTERM)
        report = json.loads((self.run_dir / "results.json").read_text())
        self.assertFalse(report["compliance_complete"])
        self.assertFalse(report["compliance_passed"])


class FioRecordingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="qemu-baseline-fio-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.jobs = self.root / "fio_tests"
        shutil.copytree(GUEST / "fio_tests", self.jobs)
        # Only relocate the disposable copy's fixed /tmp output: avoid touching
        # real host files while preserving the original comparison algorithm.
        script = self.jobs / "perf_test.sh"
        script.write_text(script.read_text().replace('"/tmp/"', '"' + str(self.root) + '/"'))
        (self.jobs / "reference_results").mkdir()
        self.binary = self.root / "bin"
        self.binary.mkdir()
        fake = self.binary / "fio"
        fake.write_text('''#!/bin/bash
count=0
if [ -f "$FIO_COUNTER" ]; then count=$(cat "$FIO_COUNTER"); fi
count=$((count + 1)); echo "$count" > "$FIO_COUNTER"
echo "invocation $count"
for direction in read write; do
    if [[ "$*" == *test_write* ]] && [ "$direction" = read ]; then continue; fi
    if [[ "$*" == *test_read* ]] && [ "$direction" = write ]; then continue; fi
    if [ "${FIO_FORMAT:-old}" = modern ]; then
        echo " $direction: IOPS=3.5k, BW=1M"
    else
        echo " $direction: io=1KB, bw=1KB/s, iops=${FIO_IOPS:-100}, runt=1msec"
    fi
done
exit "${FIO_STATUS:-0}"
''')
        fake.chmod(0o755)
        self.environment = dict(os.environ, PATH=str(self.binary) + ":" + os.environ["PATH"],
                                FIO_COUNTER=str(self.root / "counter"))

    def invoke(self, name, helper, job="write", calls=1, environment=None):
        trace = self.root / name
        result = subprocess.run(["bash", str(GUEST / "common/qemu11-baseline-fio.sh"),
            str(trace), str(calls), "./fio_jobs/test_" + job + "_short.fio", helper,
            "test_" + job + "_short"], cwd=self.jobs,
            env=dict(self.environment, **(environment or {})), capture_output=True, text=True, timeout=15)
        return trace, result

    def test_reference_and_current_measurements_keep_valid_deltas_non_gating(self):
        self.invoke("reference", "./perf_test_rec_results.sh")
        trace, process = self.invoke("compare", "./perf_test.sh", environment={"FIO_IOPS": "105"})
        self.assertEqual(0, process.returncode)
        self.assertIn("ERROR", process.stdout)  # inherited inverted percentage message
        report = baseline.fio_observations(trace, "compare-write")
        self.assertTrue(report["passed"])
        self.assertEqual({"write": 100.0}, report["reference_iops"])
        self.assertEqual({"write": 5.0}, report["measurements"][0]["delta_iops"])

    def test_mixed_comparison_preserves_both_outputs_before_overwrite(self):
        self.invoke("reference", "./perf_test_rec_results.sh", job="rw")
        trace, process = self.invoke("compare", "./perf_test.sh", job="rw", calls=2)
        self.assertEqual(0, process.returncode)
        report = baseline.fio_observations(trace, "compare-rw")
        self.assertEqual(2, len(report["invocations"]))
        self.assertTrue(report["passed"])
        self.assertEqual([{"write": 100.0, "read": 100.0}] * 2,
                         [m["iops"] for m in report["measurements"]])
        self.assertEqual([{"write": 0.0, "read": 0.0}] * 2,
                         [m["delta_iops"] for m in report["measurements"]])
        self.assertNotEqual(report["invocations"][0]["stdout"], report["invocations"][1]["stdout"])
        self.assertEqual(report["invocations"][1]["stdout"], (self.root / "test_rw_short_res").read_text())
        for invocation in report["invocations"]:
            self.assertIn("test_rw_short.fio", invocation["jobs_sha256"])
            self.assertEqual(0, invocation["exit_status"])

    def test_mixed_comparison_requires_both_directions_in_each_invocation(self):
        self.invoke("reference", "./perf_test_rec_results.sh", job="rw")
        trace, process = self.invoke("compare", "./perf_test.sh", job="rw", calls=2)
        self.assertEqual(0, process.returncode)
        invocations = sorted(trace.glob("invocation-*"), key=lambda p: (p / "started.txt").read_text())
        for index, invocation in enumerate(invocations):
            path = invocation / "stdout.log"
            original = path.read_text()
            for direction in ("read", "write"):
                for value in (None, "0", "bad"):
                    with self.subTest(invocation=index, direction=direction, value=value):
                        lines = []
                        for line in original.splitlines():
                            if line.lstrip().startswith(direction + ":"):
                                if value is None:
                                    continue
                                line = line.replace("iops=100,", "iops=" + value + ",")
                            lines.append(line)
                        path.write_text("\n".join(lines) + "\n")
                        try:
                            report = baseline.fio_observations(trace, "compare-rw")
                            self.assertFalse(report["passed"])
                            self.assertTrue(report["errors"])
                        finally:
                            path.write_text(original)

    def test_successful_wrapper_cannot_hide_failed_fio_child(self):
        wrapper = self.jobs / "hidden.sh"
        wrapper.write_text("#!/bin/bash\nfio ./fio_jobs/test_write_short.fio\necho ALL_DONE\n")
        wrapper.chmod(0o755)
        trace, process = self.invoke("hidden", "./hidden.sh", environment={"FIO_STATUS": "9"})
        self.assertNotEqual(0, process.returncode)
        self.assertEqual("0", (trace / "wrapper-exit-status.txt").read_text().strip())
        self.assertEqual("9", next(trace.glob("invocation-*/exit-status.txt")).read_text().strip())

    def test_skipped_modern_comparison_gets_labelled_fallback_and_stays_failed(self):
        self.invoke("reference", "./perf_test_rec_results.sh", job="rw", environment={"FIO_FORMAT": "modern"})
        trace, process = self.invoke("compare", "./perf_test.sh", job="rw", calls=2,
                                     environment={"FIO_FORMAT": "modern"})
        self.assertNotEqual(0, process.returncode)
        report = baseline.fio_observations(trace, "compare-rw")
        self.assertFalse(report["passed"])
        self.assertEqual(2, report["diagnostic_fallback_calls"])
        self.assertEqual({"write": 3500.0, "read": 3500.0}, report["reference_iops"])
        self.assertTrue(all(i["origin"].strip() == "diagnostic-fallback" for i in report["invocations"]))

    def test_missing_reference_or_child_output_cannot_be_measured(self):
        trace, _ = self.invoke("reference", "./perf_test_rec_results.sh")
        (trace / "reference.log").unlink()
        with self.assertRaises(OSError):
            baseline.fio_observations(trace, "record-write")
        (trace / "reference.log").write_text(" write: IOPS=100, BW=1M\n")
        next(trace.glob("invocation-*/stdout.log")).unlink()
        with self.assertRaises(OSError):
            baseline.fio_observations(trace, "record-write")

    def test_iops_parser_validates_label_direction_units_and_positive_values(self):
        self.assertEqual({"read": 12500.0, "write": 2000000.0}, baseline.directional_iops(
            " read: IOPS=12.5k, BW=1M\n write: io=1KB, bw=1KB/s, iops=2M, runt=2msec", ("read", "write")))
        for text in ("read: IOPS=bad", "read: IOPS=0", "read: IOPS=-1", "read: BW=12k",
                     "write: IOPS=12k", "read: IOPS=1\nread: IOPS=2"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                baseline.directional_iops(text, ("read",))

    def test_invalid_raw_measurement_is_a_failed_finding(self):
        trace, _ = self.invoke("reference", "./perf_test_rec_results.sh")
        next(trace.glob("invocation-*/stdout.log")).write_text("write: IOPS=bad")
        report = baseline.fio_observations(trace, "record-write")
        self.assertFalse(report["passed"])
        self.assertTrue(report["errors"])


class NoseAndSetupTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="qemu-baseline-nose-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def junit(self, group="objects_via_ioctl"):
        root = ET.Element("testsuite")
        if group == "ext4":
            ET.SubElement(root, "testcase", classname="TestExt4Format", name="test_format_all")
        else:
            for cls in ("test_NvmeCli", "test_NvmeCliMultiDisk"):
                for method in ("test_delete", "test_empty_list", "test_empty_object", "test_write_read_and_compare"):
                    ET.SubElement(root, "testcase", classname="test_nvme_cli." + cls, name=method, time="1.5")
        path = self.root / "results.xml"
        ET.ElementTree(root).write(str(path))
        return root, path

    def test_junit_distinguishes_assertion_failure_skip_and_missing_device_cases(self):
        xml, path = self.junit()
        ET.SubElement(xml[0], "failure").text = "object mismatch"
        ET.ElementTree(xml).write(str(path))
        report = baseline.nose_observations(path, "objects_via_ioctl")
        self.assertTrue(report["complete"])
        self.assertFalse(report["passed"])
        xml[0].remove(xml[0].find("failure"))
        ET.SubElement(xml[0], "skipped")
        ET.ElementTree(xml).write(str(path))
        self.assertFalse(baseline.nose_observations(path, "objects_via_ioctl")["complete"])
        xml.remove(xml[-1])
        ET.ElementTree(xml).write(str(path))
        with self.assertRaisesRegex(ValueError, "inventory"):
            baseline.nose_observations(path, "objects_via_ioctl")

    def test_duplicate_junit_results_are_rejected(self):
        xml, path = self.junit("ext4")
        ET.SubElement(xml, "testcase", classname="TestExt4Format", name="test_format_all")
        ET.ElementTree(xml).write(str(path))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            baseline.nose_observations(path, "ext4")

    def test_shared_setup_preserves_driver_binding_and_empty_skip_file(self):
        module = runpy.run_path(str(GUEST / "nvme_compliance_tests/test_nvme_compliance.py"))
        prepare = module["prepare_nvme_compliance"]
        old_directory = Path.cwd()
        os.chdir(self.root)
        self.addCleanup(os.chdir, old_directory)
        with mock.patch.object(module["subprocess"], "call", return_value=1), \
                mock.patch.object(module["subprocess"], "check_call") as checked, \
                mock.patch.object(module["os"], "system") as system, \
                mock.patch.object(module["glob"], "glob", return_value=[]):
            prepare()
        self.assertEqual("", (self.root / "skipTests").read_text())
        self.assertEqual([mock.call(["udevadm", "control", "--stop-exec-queue"]),
                          mock.call(["insmod", "dnvme.ko"]),
                          mock.call(["udevadm", "control", "--start-exec-queue"]),
                          mock.call(["udevadm", "settle"])], checked.call_args_list)
        system.assert_called_once_with("rmmod nvme")

    def test_failed_driver_load_restarts_udev_queue(self):
        module = runpy.run_path(str(GUEST / "nvme_compliance_tests/test_nvme_compliance.py"))
        def checked(command):
            if command[0] == "insmod":
                raise subprocess.CalledProcessError(1, command)
        with mock.patch.object(module["subprocess"], "call", return_value=1), \
                mock.patch.object(module["subprocess"], "check_call", side_effect=checked) as calls, \
                mock.patch.object(module["os"], "system"), self.assertRaises(subprocess.CalledProcessError):
            module["prepare_nvme_compliance"]()
        self.assertIn(mock.call(["udevadm", "control", "--start-exec-queue"]), calls.call_args_list)

    def test_normal_compliance_runner_still_stops_at_first_failed_suite(self):
        module = runpy.run_path(str(GUEST / "nvme_compliance_tests/test_nvme_compliance.py"))
        test = module["TestNVMeCompliance"]()
        with mock.patch.dict(test.test_NVMeCompliance.__globals__, {"prepare_nvme_compliance": mock.Mock()}), \
                mock.patch.object(module["os"], "system", side_effect=[0, 0, 0, 7]) as system, \
                contextlib.redirect_stdout(io.StringIO()), self.assertRaises(AssertionError):
            test.test_NVMeCompliance()
        self.assertEqual(4, system.call_count)
        self.assertIn("--test=4 ", system.call_args.args[0])


class EntryPointTests(unittest.TestCase):
    def test_wrong_configuration_fails_before_verification_or_output_creation(self):
        environment = {"EVSSIM_ENV_PATH": str(BUILDER / "env.sh"),
                       "EVSSIM_ROOT_PATH": str(SIMULATOR), "EVSSIM_BUILDER_FOLDER": "infra/builder",
                       "EVSSIM_VERSIONS_CONFIGURATION_ID": "5"}
        with mock.patch.dict(os.environ, environment, clear=True), \
                mock.patch.object(baseline.subprocess, "run") as verify, \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(1, baseline.main(["--selection", "unused.json"]))
        verify.assert_not_called()

    def test_successful_entry_records_actual_profile_sources_and_manifest(self):
        with tempfile.TemporaryDirectory(prefix="qemu-baseline-entry-") as temporary:
            root = Path(temporary)
            manifest = root / "manifest.json"
            manifest.write_text('{"key":"frozen"}\n')
            selection = root / "selection.json"
            selection.write_text(json.dumps({"status": "selected", "profile_id": 4, "manifest": str(manifest)}))
            output = root / "evidence"
            environment = {"EVSSIM_ENV_PATH": str(BUILDER / "env.sh"),
                           "EVSSIM_ROOT_PATH": str(SIMULATOR), "EVSSIM_BUILDER_FOLDER": "infra/builder",
                           "EVSSIM_LOGS_FOLDER": "logs", "EVSSIM_VERSIONS_CONFIGURATION_ID": "4"}
            with mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(baseline.subprocess, "run") as verify, \
                    mock.patch.object(baseline.subprocess, "check_output", return_value="a" * 40), \
                    mock.patch.object(baseline, "collect_baseline", return_value=0), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(0, baseline.main(["--selection", str(selection), "--output", str(output)]))
            verify.assert_called_once()
            inputs = json.loads((output / "inputs.json").read_text())
            profiles = json.loads((BUILDER / "versions/configs.json").read_text())["configs"]
            self.assertEqual(next(p for p in profiles if p["id"] == 4), inputs["profile"])
            self.assertEqual(manifest.read_bytes(), (output / "manifest.json").read_bytes())
            self.assertIn("eVSSIM/tests/guest/common/qemu11-baseline-fio.sh", inputs["guest_sources_sha256"])
            self.assertEqual([1, False], inputs["modes"]["fio"])


if __name__ == "__main__":
    unittest.main()
