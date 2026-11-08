#!/usr/bin/env python3
"""Collect the existing compatibility workloads on the selected QEMU 11 artifact.

Run from an environment sourced for this checkout and unchanged config #4.
This diagnostic command does not build inputs or invoke the normal CI workflow.
Individual failures are retained while later independent workloads continue.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
import re
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


# One shell owns the guest and its EXIT trap throughout collection. Keeping
# launch/cleanup in the same shell preserves builder.sh's container/PID state.
COMMON_RUNNER = r'''
source ./builder.sh
run_dir=$1
selection=$2
suite_timeout=$3
group=$4
version=$EVSSIM_QEMU_COMPILE_CONTAINER
export EVSSIM_QEMU_ARTIFACT_SELECTION=$selection
export EVSSIM_RUNTIME_STORAGE_STRATEGY=1
export EVSSIM_QEMU_SIMULATOR_ENABLED=yes
if [ "$group" = fio ]; then export EVSSIM_QEMU_SIMULATOR_ENABLED=no; fi
if [ "$group" = objects_via_ioctl ]; then export EVSSIM_RUNTIME_STORAGE_STRATEGY=2; fi
current_attempt=""
flush_rc=0
stop_rc=0

cleanup_guest() {
    flush_rc=0
    stop_rc=0
    if [ -n "${EVSSIM_DOCKER_UUID:-}" ]; then
        # Do not enter the general SSH helper's long retry loop on a dead VM.
        if evssim_guest_probe 5 /bin/true </dev/null; then
            if evssim_qemu_flush_disk </dev/null; then flush_rc=0; else flush_rc=$?; fi
        else
            flush_rc=1
        fi
        if evssim_qemu_stop; then stop_rc=0; else stop_rc=$?; fi
    fi
    if [ -n "$current_attempt" ]; then
        printf '%s\t%s\n' "$flush_rc" "$stop_rc" >> "$current_attempt/cleanup.tsv"
    fi
    [ "$flush_rc" -eq 0 ] && [ "$stop_rc" -eq 0 ]
}

finish() {
    local status=$?
    trap - EXIT
    if cleanup_guest >> "$run_dir/cleanup.log" 2>&1; then :; else status=1; fi
    exit "$status"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

prepare_guest() {
    # Each call is an explicit preparation/recovery attempt, never an implicit
    # switch of profile or artifact. Keep every failed attempt's logs.
    if ! evssim_qemu_fresh_image "$version" core; then return 1; fi
    if ! evssim_qemu_detached "$version"; then
        trap finish EXIT
        return 1
    fi
    # The launcher installs its own EXIT trap; restore our flush/status capture.
    trap finish EXIT
    printf '%s\n' "$EVSSIM_QEMU_LAUNCH_SELECTION" > "$current_attempt/runtime.json"
    printf '%s\n' "$EVSSIM_DOCKER_UUID" > "$current_attempt/container.txt"
    # Invoke the shared setup through its explicit entry point. This also keeps
    # Python code/quotes out of the existing SSH helper's command quoting.
    if ! evssim_guest 'cd ./guest && mkdir -p Logs/qemu11-baseline' </dev/null; then return 1; fi
    if [ "$group" = compliance ]; then
        if ! evssim_guest 'cd ./guest && sudo python nvme_compliance_tests/test_nvme_compliance.py --prepare-only' </dev/null; then return 1; fi
    elif [ "$group" = fio ]; then
        if ! evssim_guest "cd ./guest/fio_tests && sudo python -c 'from test_fio import TestFio; TestFio().test_fio_pre()'" </dev/null; then return 1; fi
    fi
    if ! evssim_guest 'cd ./guest && uname -a && python --version 2>&1 && sha256sum tnvme dnvme.ko nvme && lsmod && ls -l /sys/bus/pci/devices/*/driver' \
        > "$current_attempt/guest-identity.log" 2>&1 </dev/null; then return 1; fi
    if [ "$group" = fio ]; then
        if ! evssim_guest 'cd ./guest/fio_tests && fio --version && sha256sum fio_jobs/*.fio' \
            >> "$current_attempt/guest-identity.log" 2>&1 </dev/null; then return 1; fi
        # A recreated guest has no matching references. Re-record them using the
        # same traced helpers before any remaining comparison can be attempted.
        if [ "$attempt" -gt 1 ]; then
            local job reference_status=0
            for job in write rw read; do
                if evssim_guest "cd ./guest/fio_tests && sudo bash ../common/qemu11-baseline-fio.sh /home/esd/guest/Logs/qemu11-baseline/refresh-$job 1 ./fio_jobs/test_${job}_short.fio ./perf_test_rec_results.sh test_${job}_short" </dev/null; then :; else reference_status=1; fi
            done
            mkdir -p "$current_attempt/references"
            if evssim_copy_from_guest "$EVSSIM_GUEST_HOME_PATH/guest/Logs/qemu11-baseline" "$current_attempt/references" </dev/null; then :; else reference_status=1; fi
            if [ "$reference_status" -ne 0 ]; then return 1; fi
        fi
    fi
}
'''

COMPLIANCE_RUNNER = COMMON_RUNNER + r'''
ready=no
blocked=no
attempt=0
failed=0
for suite in {1..27}; do
    case_dir="$run_dir/compliance/suite-$suite"
    mkdir -p "$case_dir"
    if [ "$ready" = no ] && [ "$blocked" = no ]; then
        attempt=$((attempt + 1))
        current_attempt="$run_dir/compliance/attempt-$attempt"
        mkdir -p "$current_attempt"
        if prepare_guest > "$current_attempt/setup.log" 2>&1; then
            ready=yes
        else
            blocked=yes
            failed=1
            if cleanup_guest >> "$current_attempt/setup.log" 2>&1; then :; else failed=1; fi
        fi
    fi
    if [ "$blocked" = yes ]; then
        printf 'blocked\t-\t0\t%s\n' "$attempt" > "$case_dir/result.tsv"
        continue
    fi
    # Verify the frozen selection and actual running process again before each
    # suite, so later results cannot inherit identity from an earlier guest.
    if ! python3 "$EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/qemu-artifact.py" \
        --runtime-selection "$selection" --running-container "$EVSSIM_DOCKER_UUID" \
        --profiles "$EVSSIM_ROOT_PATH/$EVSSIM_VERSIONS_FOLDER/configs.json" \
        > "$case_dir/runtime.json" 2> "$case_dir/identity.log"; then
        printf 'blocked\t-\t0\t%s\n' "$attempt" > "$case_dir/result.tsv"
        failed=1
        blocked=yes
        continue
    fi
    command="cd ./guest && sudo ./tnvme --rev=1.2 --test=$suite --skiptest=skipTests"
    if [ -n "$suite_timeout" ]; then
        command="cd ./guest && sudo timeout $suite_timeout ./tnvme --rev=1.2 --test=$suite --skiptest=skipTests"
    fi
    printf '%s\n' "$command" > "$case_dir/command.txt"
    started=$SECONDS
    if evssim_guest "$command" > "$case_dir/output.log" 2>&1 </dev/null; then
        status=0
    else
        status=$?
        failed=1
    fi
    printf 'executed\t%s\t%s\t%s\n' "$status" "$((SECONDS - started))" "$attempt" > "$case_dir/result.tsv"
    # An ordinary suite failure does not force image replacement. Recover only
    # when the process/SSH is unavailable, repeating setup on the same artifact.
    if [ "$status" -ne 0 ]; then
        if evssim_qemu_is_running && evssim_guest_probe 5 /bin/true </dev/null; then
            :
        else
            if cleanup_guest >> "$case_dir/recovery.log" 2>&1; then :; else failed=1; fi
            ready=no
        fi
    fi
done
exit "$failed"
'''


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def compliance_results(run_dir):
    """Missing or blocked records cannot become successful suite results."""
    results = []
    for suite in range(1, 28):
        directory = run_dir / "compliance" / ("suite-" + str(suite))
        result = {"suite": suite, "status": "blocked", "exit_status": None,
                  "elapsed_seconds": None, "attempt": None,
                  "reason": "collector stopped before recording a suite result"}
        record = directory / "result.tsv"
        if record.is_file():
            try:
                status, code, elapsed, attempt = record.read_text().strip().split("\t")
                result.update(status=status, exit_status=None if code == "-" else int(code),
                              elapsed_seconds=int(elapsed), attempt=int(attempt))
                result.pop("reason")
                if status == "blocked":
                    result["reason"] = "guest setup/recovery or runtime identity failed; see attempt/case logs"
                elif status != "executed":
                    raise ValueError("unknown compliance status: " + status)
            except ValueError as error:
                # An interruption can leave a partial record. Keep other suite
                # results and report this one as incomplete, never as passed.
                result.update(status="incomplete", reason="invalid suite result: " + str(error))
                results.append(result)
                continue
        for name, filename in (("command", "command.txt"), ("raw_output", "output.log"),
                               ("runtime_identity", "runtime.json")):
            path = directory / filename
            if path.is_file():
                result[name] = path.read_text().strip() if name == "command" else str(path.relative_to(run_dir))
        if result["status"] == "executed":
            if not all(name in result for name in ("command", "raw_output", "runtime_identity")):
                result.update(status="incomplete", reason="required command/output/runtime evidence missing")
            else:
                try:
                    identity = json.loads((directory / "runtime.json").read_text())
                except ValueError:
                    identity = None
                expected = json.loads((run_dir / "selection.json").read_text())
                if (not isinstance(identity, dict)
                        or any(identity.get(k) != v for k, v in expected.items())):
                    result.update(status="incomplete", reason="recorded runtime differs from frozen selection")
                attempt = run_dir / "compliance" / ("attempt-" + str(result["attempt"]))
                guest_identity = attempt / "guest-identity.log"
                if not guest_identity.is_file() or not guest_identity.stat().st_size:
                    result.update(status="incomplete", reason="guest/tool identity evidence missing")
                else:
                    result["guest_identity"] = str(guest_identity.relative_to(run_dir))
            if result["exit_status"] == 255:
                # SSH and a remote child can both return 255. Without separate
                # completion evidence, neither establishes a completed suite.
                result.update(status="incomplete", reason="SSH exit 255: remote completion unconfirmed")
        results.append(result)
    return results


def cleanup_results(run_dir, group="compliance"):
    records = []
    for attempt in sorted((run_dir / group).glob("attempt-*")):
        path = attempt / "cleanup.tsv"
        events = []
        if path.is_file():
            for line in path.read_text().splitlines():
                flush, stop = line.split("\t")
                events.append({"flush_exit_status": int(flush), "stop_exit_status": int(stop)})
        records.append({"attempt": int(attempt.name.split("-")[-1]),
                        "events": events, "recorded": bool(events)})
    return records


def run_shell(builder, run_dir, suite_timeout, runner, group):
    with (run_dir / (group + "-collector.log")).open("w") as log:
        process = subprocess.Popen(
            ["bash", "-c", runner, "s5-" + group, str(run_dir),
             str(run_dir / "selection.json"), "" if suite_timeout is None else str(suite_timeout), group],
            cwd=str(builder), stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
            start_new_session=True)
        try:
            status = process.wait()
        except BaseException:
            # Signal the session, including SSH, so Bash can reach its EXIT
            # trap instead of waiting indefinitely for an interrupted suite.
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    status = process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    status = process.wait()
            raise
    return status


def collect_compliance(builder, run_dir, selection, suite_timeout=None):
    """Run only compliance; leave the overall S5 baseline explicitly incomplete."""
    frozen = run_dir / "selection.json"
    write_json(frozen, selection)
    started = time.monotonic()
    runner_status = None
    try:
        runner_status = run_shell(builder, run_dir, suite_timeout, COMPLIANCE_RUNNER, "compliance")
    finally:
        results = compliance_results(run_dir)
        complete = all(r["status"] == "executed" for r in results)
        cleanup = cleanup_results(run_dir)
        cleanup_passed = bool(cleanup) and all(
            r["recorded"] and all(e["flush_exit_status"] == 0 and e["stop_exit_status"] == 0
                                  for e in r["events"]) for r in cleanup)
        passed = (complete and runner_status == 0 and cleanup_passed
                  and all(r["exit_status"] == 0 for r in results))
        report = {"schema_version": 1, "scope": "compliance-only",
                  "baseline_complete": False, "compliance_complete": complete,
                  "compliance_passed": passed, "runner_exit_status": runner_status,
                  "elapsed_seconds": time.monotonic() - started,
                  "profile": selection["profile_id"], "selection": "selection.json",
                  "cleanup_passed": cleanup_passed, "cleanup": cleanup,
                  "pending_groups": ["fio", "ext4", "objects_via_ioctl"],
                  "results": results}
        write_json(run_dir / "results.json", report)
    return 0 if passed else 1


# Keep the original nose groups intact. JUnit supplies per-method results without
# changing their shared state, discovery order, assertions or two-device setup.
WORKLOAD_RUNNER = COMMON_RUNNER + r'''
ready=no
blocked=no
attempt=0
failed=0
while IFS=$'\t' read -r case_id command <&3; do
    case_dir="$run_dir/$group/$case_id"
    mkdir -p "$case_dir"
    if [ "$ready" = no ] && [ "$blocked" = no ]; then
        attempt=$((attempt + 1))
        current_attempt="$run_dir/$group/attempt-$attempt"
        mkdir -p "$current_attempt"
        if prepare_guest > "$current_attempt/setup.log" 2>&1; then ready=yes; else
            blocked=yes
            failed=1
            if cleanup_guest >> "$current_attempt/setup.log" 2>&1; then :; else failed=1; fi
        fi
    fi
    if [ "$blocked" = yes ]; then
        printf 'blocked\t-\t0\t%s\t-\n' "$attempt" > "$case_dir/result.tsv"
        continue
    fi
    if ! python3 "$EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/qemu-artifact.py" \
        --runtime-selection "$selection" --running-container "$EVSSIM_DOCKER_UUID" \
        --profiles "$EVSSIM_ROOT_PATH/$EVSSIM_VERSIONS_FOLDER/configs.json" \
        > "$case_dir/runtime.json" 2> "$case_dir/identity.log"; then
        printf 'blocked\t-\t0\t%s\t-\n' "$attempt" > "$case_dir/result.tsv"
        failed=1
        blocked=yes
        continue
    fi
    command=${command//__DEVICES__/$EVSSIM_PROVISIONED_DEVICE_COUNT}
    printf '%s\n' "$command" > "$case_dir/command.txt"
    started=$SECONDS
    if evssim_guest "$command" > "$case_dir/output.log" 2>&1 </dev/null; then status=0; else status=$?; failed=1; fi
    elapsed=$((SECONDS - started))
    mkdir -p "$case_dir/guest"
    if evssim_copy_from_guest "$EVSSIM_GUEST_HOME_PATH/guest/Logs/qemu11-baseline/$case_id" "$case_dir/guest" \
        > "$case_dir/collection.log" 2>&1 </dev/null; then collection_status=0; else collection_status=$?; failed=1; fi
    printf 'executed\t%s\t%s\t%s\t%s\n' "$status" "$elapsed" "$attempt" "$collection_status" > "$case_dir/result.tsv"
    if [ "$status" -ne 0 ] || [ "$collection_status" -ne 0 ]; then
        if evssim_qemu_is_running && evssim_guest_probe 5 /bin/true </dev/null; then :; else
            if cleanup_guest >> "$case_dir/recovery.log" 2>&1; then :; else failed=1; fi
            ready=no
        fi
    fi
done 3< "$run_dir/$group/cases.tsv"
exit "$failed"
'''


SHORT_DIRECTIONS = {"write": ("write",), "rw": ("write", "read"), "read": ("read",)}
OBJECT_METHODS = ("test_delete", "test_empty_list", "test_empty_object", "test_write_read_and_compare")


def workload_cases(group, timeout=None):
    if group == "fio":
        cases = []
        for phase, helper in (("record", "perf_test_rec_results.sh"), ("compare", "perf_test.sh")):
            for job, directions in SHORT_DIRECTIONS.items():
                name = phase + "-" + job
                calls = len(directions) if phase == "compare" else 1
                cases.append((name, "./fio_jobs/test_" + job + "_short.fio", calls,
                              "./" + helper + " test_" + job + "_short"))
        cases.extend((("surface", "./fio_jobs/surface-scan.fio", 1, "fio ./fio_jobs/surface-scan.fio"),
                      ("multi", "./fio_jobs/test_multi_disk.fio", 1, "./perf_test_multi_disks.sh")))
        prefix = "sudo " + ("timeout " + str(timeout) + " " if timeout else "")
        return [(name, "cd ./guest/fio_tests && " + prefix +
                 "bash ../common/qemu11-baseline-fio.sh /home/esd/guest/Logs/qemu11-baseline/" +
                 name + " " + str(calls) + " " + job + " " + helper)
                for name, job, calls, helper in cases]
    prefix = "sudo " + ("timeout " + str(timeout) + " " if timeout else "")
    # env must follow timeout, since timeout expects an executable, not NAME=VALUE.
    command = ("cd ./guest && mkdir -p Logs/qemu11-baseline/" + group + " && " + prefix +
               "env VSSIM_NEXTGEN_BUILD_SYSTEM=1 EVSSIM_PROVISIONED_DEVICE_COUNT=__DEVICES__ "
               "nosetests -v -s --with-xunit --xunit-file=Logs/qemu11-baseline/" + group +
               "/results.xml " + group)
    return [(group, command)]


def directional_iops(text, directions):
    """Read labelled old/new fio text; never strip digits or infer directions."""
    metrics = {}
    for direction in directions:
        pattern = (r"^\s*" + direction +
                   r"\s*:\s*[^\n]*?\biops\s*=\s*([0-9]+(?:\.[0-9]+)?)([kKmMgG]?)\b")
        matches = re.findall(pattern, text, flags=re.IGNORECASE | re.MULTILINE)
        if len(matches) != 1:
            raise ValueError("expected exactly one labelled " + direction + " IOPS value")
        value, suffix = matches[0]
        value = float(value) * {"": 1, "k": 1000, "m": 1000000, "g": 1000000000}[suffix.lower()]
        if not math.isfinite(value) or value <= 0:
            raise ValueError("invalid " + direction + " IOPS value")
        metrics[direction] = value
    return metrics


def fio_observations(directory, case_id):
    invocations = []
    errors = []
    for path in sorted(directory.glob("invocation-*"),
                       key=lambda p: (p / "started.txt").read_text() if (p / "started.txt").exists() else ""):
        entry = {"path": path.name}
        for key, filename in (("command", "command.txt"), ("stdout", "stdout.log"),
                              ("stderr", "stderr.log"), ("jobs_sha256", "jobs.sha256"),
                              ("origin", "origin.txt")):
            entry[key] = (path / filename).read_text()
        entry["exit_status"] = int((path / "exit-status.txt").read_text())
        entry["elapsed_seconds"] = int((path / "elapsed-seconds.txt").read_text())
        if entry["exit_status"] != 0 or re.search(r"\berr=\s*[1-9][0-9]*", entry["stdout"]):
            errors.append("fio invocation failed or reported a verification/I/O error: " + path.name)
        invocations.append(entry)
    phase, _, job = case_id.partition("-")
    directions = SHORT_DIRECTIONS.get(job, ())
    expected = len(directions) if phase == "compare" else 1
    if len(invocations) != expected:
        raise ValueError("missing or unexpected fio invocations for " + case_id)
    result = {"invocations": invocations,
              "wrapper_exit_status": int((directory / "wrapper-exit-status.txt").read_text())}
    missing = directory / "missing-wrapper-invocations.txt"
    result["diagnostic_fallback_calls"] = int(missing.read_text()) if missing.exists() else 0
    if directions:
        raw_reference = (directory / "reference.log").read_text()
        try:
            reference = directional_iops(raw_reference, directions)
            result["reference_iops"] = reference
            observations = []
            for invocation in invocations:
                # Each mixed job performs both directions, including when the
                # legacy wrapper reruns it to compare one direction at a time.
                metrics = directional_iops(invocation["stdout"], directions)
                observations.append({"iops": metrics, "delta_iops": {
                    direction: value - reference[direction] for direction, value in metrics.items()}})
            result["measurements"] = observations
        except ValueError as error:
            # A fully captured failed/invalid measurement is a research finding,
            # distinct from missing evidence or an unexecuted workload.
            errors.append(str(error))
    # Valid deltas are observations, never an invented percentage gate.
    result["errors"] = errors
    result["passed"] = (result["wrapper_exit_status"] == 0
                        and not result["diagnostic_fallback_calls"] and not errors)
    return result


def nose_observations(path, group):
    root = ET.parse(path).getroot()
    cases = []
    expected = ({"test_format_all"} if group == "ext4" else
                {cls + "." + method for cls in ("test_NvmeCli", "test_NvmeCliMultiDisk")
                 for method in OBJECT_METHODS})
    observed = set()
    for node in root.iter("testcase"):
        name = node.get("name", "")
        classname = node.get("classname", "")
        identifier = name if group == "ext4" else classname.rsplit(".", 1)[-1] + "." + name
        if group == "objects_via_ioctl" and name in ("test_overwrite", "test_max_size_object"):
            cases.append({"case": identifier, "status": "excluded", "reason": "inherited no-op/placeholder"})
            continue
        status = "passed"
        if node.find("failure") is not None or node.find("error") is not None:
            status = "failed"
        elif node.find("skipped") is not None:
            status = "blocked"
        cases.append({"case": identifier, "status": status, "elapsed_seconds": node.get("time")})
        if identifier in observed:
            raise ValueError("duplicate JUnit result for " + identifier)
        observed.add(identifier)
    if observed != expected:
        raise ValueError("JUnit inventory differs from enabled " + group + " cases")
    return {"cases": cases, "passed": all(c["status"] in ("passed", "excluded") for c in cases),
            "complete": all(c["status"] != "blocked" for c in cases)}


def workload_results(run_dir, group, cases):
    results = []
    selection = json.loads((run_dir / "selection.json").read_text())
    for case_id, command in cases:
        directory = run_dir / group / case_id
        result = {"case": case_id, "command": command, "status": "blocked", "exit_status": None,
                  "reason": "setup/recovery/identity failed or no execution result was recorded"}
        try:
            fields = (directory / "result.tsv").read_text().strip().split("\t")
            status, code, elapsed, attempt, copied = fields
            result.update(attempt=int(attempt), elapsed_seconds=int(elapsed))
            if status != "executed":
                results.append(result)
                continue
            result.update(status="executed", exit_status=int(code), collection_exit_status=int(copied))
            result.pop("reason")
            result["command"] = (directory / "command.txt").read_text().strip()
            result["raw_output"] = str((directory / "output.log").relative_to(run_dir))
            (directory / "output.log").read_bytes()
            runtime = json.loads((directory / "runtime.json").read_text())
            if not isinstance(runtime, dict) or any(runtime.get(k) != v for k, v in selection.items()):
                raise ValueError("running artifact differs from frozen selection")
            result["runtime_identity"] = str((directory / "runtime.json").relative_to(run_dir))
            identity = run_dir / group / ("attempt-" + attempt) / "guest-identity.log"
            if not identity.read_text().strip():
                raise ValueError("guest/tool identity missing")
            result["guest_identity"] = str(identity.relative_to(run_dir))
            # Collected files do not confirm that the remote command completed
            # when SSH returned its ambiguous transport-error status.
            if int(code) == 255:
                raise ValueError("SSH exit 255: remote completion unconfirmed")
            if int(copied) != 0:
                raise ValueError("guest evidence collection failed")
            guest = directory / "guest" / case_id
            if group == "fio":
                result["observations"] = fio_observations(guest, case_id)
            else:
                result["observations"] = nose_observations(guest / "results.xml", group)
            result["passed"] = int(code) == 0 and result["observations"]["passed"]
        except (OSError, ValueError, ET.ParseError) as error:
            result.update(status="incomplete", passed=False, reason=str(error))
        results.append(result)
    return results


def collect_workload(builder, run_dir, selection, group, timeout=None):
    cases = workload_cases(group, timeout)
    directory = run_dir / group
    directory.mkdir()
    (directory / "cases.tsv").write_text("".join(name + "\t" + cmd + "\n" for name, cmd in cases))
    started = time.monotonic()
    status = None
    try:
        status = run_shell(builder, run_dir, timeout, WORKLOAD_RUNNER, group)
    finally:
        results = workload_results(run_dir, group, cases)
        cleanup = cleanup_results(run_dir, group)
        complete = all(r["status"] == "executed" and r.get("observations", {}).get("complete", True)
                       for r in results)
        cleaned = bool(cleanup) and all(r["recorded"] and all(
            e["flush_exit_status"] == 0 and e["stop_exit_status"] == 0 for e in r["events"]) for r in cleanup)
        report = {"group": group, "complete": complete,
                  "passed": complete and cleaned and status == 0 and all(r.get("passed") for r in results),
                  "runner_exit_status": status, "elapsed_seconds": time.monotonic() - started,
                  "cleanup_passed": cleaned, "cleanup": cleanup, "results": results}
        write_json(run_dir / (group + "-results.json"), report)
    return report


def collect_baseline(builder, run_dir, selection, timeout=None):
    # A group's functional failure does not prevent independent later groups.
    collect_compliance(builder, run_dir, selection, timeout)
    compliance = json.loads((run_dir / "results.json").read_text())
    write_json(run_dir / "compliance-results.json", compliance)
    reports = {"compliance": compliance}
    try:
        for group in ("fio", "ext4", "objects_via_ioctl"):
            reports[group] = collect_workload(builder, run_dir, selection, group, timeout)
    finally:
        complete = (len(reports) == 4 and compliance["compliance_complete"] and
                    all(r["complete"] for name, r in reports.items() if name != "compliance"))
        passed = complete and compliance["compliance_passed"] and all(
            r["passed"] for name, r in reports.items() if name != "compliance")
        write_json(run_dir / "results.json", {"schema_version": 1, "scope": "compatibility-baseline",
                   "baseline_complete": complete, "baseline_passed": passed,
                   "profile_id": selection["profile_id"], "groups": reports,
                   "pending_groups": [g for g in ("fio", "ext4", "objects_via_ioctl") if g not in reports]})
    return 0 if passed else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selection", required=True, type=Path,
                        help="S2c artifact selection for unchanged config #4")
    parser.add_argument("--output", type=Path, help="new evidence directory (must not exist)")
    parser.add_argument("--suite-timeout", type=int,
                        help="optional explicit suite/workload timeout in seconds; no guessed default")
    args = parser.parse_args(argv)
    if args.suite_timeout is not None and args.suite_timeout <= 0:
        parser.error("--suite-timeout must be positive")
    builder = Path(__file__).resolve().parent
    try:
        # Match the established environment to this checkout; env.sh derives its
        # root from cwd, so silently sourcing it inside a linked worktree is wrong.
        if Path(os.environ.get("EVSSIM_ENV_PATH", "")).resolve() != builder / "env.sh":
            raise ValueError("source this checkout's infra/builder/env.sh before collection")
        runtime_builder = Path(os.environ.get("EVSSIM_ROOT_PATH", "")) / os.environ.get(
            "EVSSIM_BUILDER_FOLDER", "")
        if runtime_builder.resolve() != builder:
            raise ValueError("loaded environment root does not map to this builder checkout")
        if os.environ.get("EVSSIM_VERSIONS_CONFIGURATION_ID") != "4":
            raise ValueError("S5 requires the explicitly selected, unchanged config #4")
        selection = json.loads(args.selection.read_text())
        if selection.get("status") != "selected" or selection.get("profile_id") != 4:
            raise ValueError("S5 requires a successful config #4 artifact selection")
        # Use the existing production verifier before preparing/replacing a guest.
        subprocess.run([sys.executable, str(builder / "qemu-artifact.py"),
                        "--runtime-selection", str(args.selection.resolve()),
                        "--profiles", str(builder / "versions/configs.json")],
                       check=True, stdout=subprocess.DEVNULL)
        base = Path(os.environ["EVSSIM_ROOT_PATH"]) / os.environ["EVSSIM_LOGS_FOLDER"]
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        output = (args.output or base / "qemu11-validation" / (stamp + "-s5")).resolve()
        output.mkdir(parents=True, exist_ok=False)
        shutil.copyfile(selection["manifest"], output / "manifest.json")
        profile_bytes = (builder / "versions/configs.json").read_bytes()
        write_json(output / "inputs.json", {
            "story": "S5", "scope": "compatibility-baseline", "builder": str(builder),
            "profile": next(p for p in json.loads(profile_bytes)["configs"] if p["id"] == 4),
            "profiles_sha256": hashlib.sha256(profile_bytes).hexdigest(),
            "collector_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "compliance_source_sha256": hashlib.sha256(
                (builder.parent.parent / "eVSSIM/tests/guest/nvme_compliance_tests/"
                 "test_nvme_compliance.py").read_bytes()).hexdigest(),
            "guest_sources_sha256": {str(path.relative_to(builder.parent.parent)):
                hashlib.sha256(path.read_bytes()).hexdigest() for path in
                (builder.parent.parent / "eVSSIM/tests/guest").rglob("*")
                if path.is_file() and path.suffix in (".py", ".sh", ".fio")},
            "suite_timeout_seconds": args.suite_timeout,
            "modes": {"compliance": [1, True], "fio": [1, False],
                      "ext4": [1, True], "objects_via_ioctl": [2, True]},
            "source_revision": subprocess.check_output(
                ["git", "-C", str(builder), "rev-parse", "HEAD"], text=True).strip(),
            "invocation": shlex.join([sys.executable, str(Path(__file__).resolve())] +
                                     (sys.argv[1:] if argv is None else argv))})
        print("Compatibility evidence: " + str(output), flush=True)
        return collect_baseline(builder, output, selection, args.suite_timeout)
    except KeyboardInterrupt:
        print("ERROR: collection interrupted; inspect cleanup records before another run", file=sys.stderr)
        return 130
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    def terminate(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, terminate)
    sys.exit(main())
