#!/usr/bin/env python3
"""Retain and check evidence for the selected local QEMU workflow."""

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


SPEC = importlib.util.spec_from_file_location("artifact", Path(__file__).with_name("qemu-artifact.py"))
artifact = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(artifact)
FIO = Path(__file__).resolve().parents[2] / "eVSSIM/tests/guest/fio_tests"
SPEC = importlib.util.spec_from_file_location("fio_metrics", FIO / "fio_metrics.py")
fio_metrics = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fio_metrics)
STAGES = ("resolve-inputs", "workflow-inputs", "check-tools", "tox", "build-images", "container-images",
          "qemu-sources", "guest-image", "kernel", "qemu-artifact", "host-tools", "guest-tools",
          "exofs-tools", "nvme-cli", "sanity", "elk-start", "host-tests", "host-metrics",
          "guest-tests", "exofs", "cleanup")
REPOSITORIES = ("simulator", "qemu", "kernel", "nvme-cli", "nvmeCompl/tnvme",
                "nvmeCompl/dnvme", "open-osd")
ARTIFACT_STAGES = STAGES[STAGES.index("host-tools"):-1]


def read(path):
    return json.loads(Path(path).read_text())


def write(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def digest(path):
    checksum = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def sources(root):
    return {name: subprocess.check_output(
        ["git", "-C", str(root / name), "rev-parse", "HEAD"], text=True).strip()
        for name in REPOSITORIES}


def profile():
    configs = read(Path(os.environ["EVSSIM_ROOT_PATH"]) / os.environ["EVSSIM_VERSIONS_FOLDER"] / "configs.json")
    return next(p for p in configs["configs"] if p["id"] == int(os.environ["EVSSIM_VERSIONS_CONFIGURATION_ID"]))


def record_inputs(directory):
    root = Path(os.environ["EVSSIM_ROOT_PATH"])
    snapshot = {"profile": profile(), "sources": sources(root)}
    if (directory / "resolved-inputs.json").exists():
        dependencies = read(directory / "resolved-inputs.json")["dependencies"]
    else:
        dependencies = read(directory / "inputs.json").get("dependency_report", [])
    # A retained report can outlive its checkout. Every applied dependency
    # must still be present in the source that this invocation will build.
    for dependency in dependencies:
        name = dependency["project"]
        if name in ("tnvme", "dnvme"):
            name = "nvmeCompl/" + name
        subprocess.run(["git", "-C", str(root / name), "merge-base", "--is-ancestor",
                        dependency["applied_revision"], snapshot["sources"][name]], check=True)
    write(directory / "workflow-inputs.json", snapshot)


def images(selected_profile):
    containers = {value for section in selected_profile.values() if isinstance(section, dict)
                  for field, value in section.items() if field in ("compileContainer", "runContainer")}
    return {name: artifact.inspect_image(os.environ["EVSSIM_DOCKER_IMAGE_NAME"] + ":" + name)
            for name in sorted(containers)}


def run_stage(directory, name, command, stdout=None):
    folder = directory / "stages" / name
    folder.mkdir(parents=True)
    order_path = directory / "stages.json"
    order = read(order_path) if order_path.exists() else []
    write(order_path, order + [name])
    selection = os.environ.get("EVSSIM_QEMU_ARTIFACT_SELECTION")
    record = {"name": name, "argv": command, "started_at": datetime.now(timezone.utc).isoformat(),
              "profile_id": None if name == "resolve-inputs" else int(os.environ["EVSSIM_VERSIONS_CONFIGURATION_ID"]),
              "artifact_key": read(selection)["key"] if selection and name in ARTIFACT_STAGES else None,
              "exit_status": None, "child_exit_status": None, "elapsed_seconds": None, "files": {}}
    write(folder / "result.json", record)
    started = time.monotonic()
    status, logging_failed = 1, False
    output = None
    try:
        environment = dict(os.environ)
        environment.pop("EVSSIM_WORKFLOW_STAGE_DIR", None)
        if name in ("sanity", "guest-tests", "exofs"):
            environment["EVSSIM_WORKFLOW_STAGE_DIR"] = str(folder)
        if stdout:
            output = Path(stdout).open("wb")
        with (folder / "output.log").open("wb") as log:
            child = subprocess.Popen(command, stdout=output or subprocess.PIPE,
                stderr=subprocess.PIPE if output else subprocess.STDOUT,
                env=environment)
            stream = child.stderr if output else child.stdout
            for block in iter(lambda: stream.read1(65536), b""):
                try:
                    log.write(block)
                    log.flush()
                    sys.stdout.buffer.write(block)
                    sys.stdout.buffer.flush()
                except OSError:
                    logging_failed = True
            stream.close()
            status = child.wait()
            record["child_exit_status"] = status
            if status < 0:
                status = 128 - status
    except (OSError, ValueError) as error:
        record["error"] = str(error)
    finally:
        if output:
            output.close()
    if status == 0 and logging_failed:
        status = 1
    record.update(exit_status=status, logging_failed=logging_failed, elapsed_seconds=time.monotonic() - started)
    # Child status wins over a subsequent evidence-writing failure.
    try:
        if name == "qemu-artifact":
            # Staging stays outside the logs copied to Podman. Retain build
            # diagnostics on success and failure without copying generated links.
            build_logs = Path(command[command.index("--build-dir") + 1]) / "logs"
            if build_logs.is_dir():
                shutil.copytree(build_logs, directory / "qemu/logs")
        if name == "qemu-artifact" and status == 0:
            selected = read(stdout)
            record["artifact_key"] = selected["key"]
            shutil.copy2(selected["manifest"], directory / "artifact-manifest.json")
        if name == "resolve-inputs" and status == 0:
            selected = read(stdout)
            record["profile_id"] = selected.get("profile", selected)["id"]
        for path in folder.rglob("*"):
            if path.is_file() and path != folder / "result.json":
                record["files"][str(path.relative_to(folder))] = digest(path)
        if stdout:
            record["stdout"] = {"path": str(Path(stdout).relative_to(directory)), "sha256": digest(stdout)}
        write(folder / "result.json", record)
    except (OSError, ValueError, KeyError) as error:
        print("ERROR Retaining stage evidence: " + str(error), file=sys.stderr)
        return status or 1
    return status


def record_launch(directory, runtime, container, count, strategy, simulator, config):
    folder = directory / "launches"
    folder.mkdir(exist_ok=True)
    name = str(len(list(folder.glob("*.json"))) + 1)
    copied = folder / (name + ".ssd.conf")
    shutil.copy2(config, copied)
    write(folder / (name + ".json"), {"runtime": runtime, "container": container,
          "device_count": count, "strategy": strategy, "simulator": simulator,
          "ssd_config": copied.name, "ssd_sha256": digest(copied)})


def validate_guest_results(folder):
    for number in range(1, 5):
        result = folder / "guest" / str(number)
        suites = list(ET.parse(result / "guest_tests_results.xml").getroot().iter("testsuite"))
        # Nose reports "skip"; other JUnit producers use "skipped" or only
        # testcase markers. Partial skips are valid, but each group must run.
        executed = sum(int(s.attrib["tests"]) - max(
            int(s.get("skip", 0)), int(s.get("skipped", 0)),
            len(s.findall("testcase/skipped"))) for s in suites)
        if (not suites or executed <= 0
                or any(int(s.get("failures", 0)) or int(s.get("errors", 0)) for s in suites)):
            raise ValueError("guest JUnit reports no executed tests or a failure: " + str(number))
        if number == 1 and not (result / "nvme-compliance-logs.tar.gz").is_file():
            raise ValueError("missing raw compliance log archive")
        checksums = dict(line.split()[::-1] for line in (result / "guest-tools.sha256").read_text().splitlines())
        dist = Path(os.environ["EVSSIM_ROOT_PATH"]) / os.environ["EVSSIM_DIST_FOLDER"]
        # Match upstream's guest-root staging tree and the collector's file labels.
        tools = Path(os.environ["EVSSIM_ROOT_PATH"]) / os.environ["EVSSIM_TOOLS_FOLDER"]
        guest = tools / os.environ["EVSSIM_GUEST_HOME_PATH"].lstrip("/") / "guest"
        payload = {"/usr/local/sbin/nvme": tools / "usr/local/sbin/nvme",
                   "tnvme": guest / "tnvme", "dnvme.ko": guest / "dnvme.ko"}
        if checksums != {name: digest(path) for name, path in payload.items()}:
            raise ValueError("guest tools differ from built payload: " + str(number))
        kernelrelease = (dist / "kernel/kernelrelease").read_text().strip()
        if (result / "guest-kernel.txt").read_text().strip() != kernelrelease:
            raise ValueError("guest kernel differs from selected payload: " + str(number))
    fio = folder / "guest/2/fio"
    for job in ("test_write_short", "test_rw_short", "test_read_short"):
        saved_job = fio / "jobs" / (job + ".fio")
        if digest(saved_job) != digest(FIO / "fio_jobs" / (job + ".fio")):
            raise ValueError("retained fio job differs from selected source: " + job)
        directions = fio_metrics.directions(str(saved_job))
        fio_metrics.iops(str(fio / "references" / (job + "_ref_res")), directions)
        outputs = list((fio / "current").glob(job + "_*_res.*"))
        if len(outputs) < len(directions):
            raise ValueError("missing raw fio invocation: " + job)
        for output in outputs:
            fio_metrics.iops(str(output), directions)


def validate(directory):
    if read(directory / "stages.json") != list(STAGES):
        raise ValueError("required workflow stages are missing or out of order")
    snapshot = read(directory / "workflow-inputs.json")
    if snapshot != {"profile": profile(), "sources": sources(Path(os.environ["EVSSIM_ROOT_PATH"]))}:
        raise ValueError("profile or source revisions changed during the workflow")
    if read(directory / "container-images.json") != images(snapshot["profile"]):
        raise ValueError("configured container images changed during the workflow")
    selection = read(directory / "artifact-selection.json")
    manifest = read(directory / "artifact-manifest.json")
    if (directory / "resolved-inputs.json").exists():
        resolved = read(directory / "resolved-inputs.json")
        report = read(directory / "dependencies.json")
        if report["status"] != "complete" or report["dependencies"] != resolved["dependencies"]:
            raise ValueError("dependency outcomes differ from resolved inputs")
        dependencies = resolved["dependencies"]
    else:
        dependencies = read(directory / "inputs.json").get("dependency_report", [])
    if selection.get("dependency_report", []) != dependencies:
        raise ValueError("artifact dependency provenance differs from workflow inputs")
    if manifest != read(selection["manifest"]):
        raise ValueError("retained artifact manifest differs from the selected bundle")
    if manifest["inputs"]["profile"] != snapshot["profile"]:
        raise ValueError("artifact profile differs from workflow inputs")
    for name in ("qemu", "simulator"):
        if selection["sources"][name] != snapshot["sources"][name]:
            raise ValueError("artifact source revision differs from workflow inputs: " + name)
    configs = read(Path(os.environ["EVSSIM_ROOT_PATH"]) / os.environ["EVSSIM_VERSIONS_FOLDER"] / "configs.json")
    runtime = artifact.runtime_selection(selection, configs, os.environ)
    expected_launches = {"sanity": 1, "guest-tests": 4, "exofs": 1}
    for name in STAGES:
        folder = directory / "stages" / name
        stage = read(folder / "result.json")
        if (stage["name"] != name or not stage["argv"] or stage["elapsed_seconds"] is None
                or stage["elapsed_seconds"] < 0 or stage["exit_status"] is None):
            raise ValueError("incomplete stage evidence: " + name)
        if name != "cleanup" and (stage["exit_status"] != 0 or stage["child_exit_status"] != 0
                                  or stage["logging_failed"]):
            raise ValueError("required stage failed: " + name)
        if name != "resolve-inputs" and stage["profile_id"] != snapshot["profile"]["id"]:
            raise ValueError("stage profile differs from selected profile: " + name)
        if name in ARTIFACT_STAGES:
            if stage["artifact_key"] != selection["key"]:
                raise ValueError("stage artifact differs from selected bundle: " + name)
        if "output.log" not in stage["files"]:
            raise ValueError("missing stage output: " + name)
        actual_files = {str(p.relative_to(folder)): digest(p) for p in folder.rglob("*")
                        if p.is_file() and p != folder / "result.json"}
        if actual_files != stage["files"]:
            raise ValueError("stage outputs changed or are missing: " + name)
        if "stdout" in stage:
            if digest(directory / stage["stdout"]["path"]) != stage["stdout"]["sha256"]:
                raise ValueError("stage JSON output changed: " + name)
        launches = sorted((folder / "launches").glob("*.json"))
        if len(launches) != expected_launches.get(name, 0):
            raise ValueError("missing or unexpected launch records: " + name)
        modes = {"sanity": [(1, "yes")], "guest-tests": [(1, "yes"), (1, "no"), (1, "yes"), (2, "yes")],
                 "exofs": [(2, "yes")]}
        for number, path in enumerate(launches):
            launch = read(path)
            if (launch["runtime"] != runtime or not launch["container"] or launch["device_count"] <= 0
                    or (launch["strategy"], launch["simulator"]) != modes[name][number]
                    or digest(path.parent / launch["ssd_config"]) != launch["ssd_sha256"]):
                raise ValueError("launch identity/configuration differs: " + str(path))
        if name == "guest-tests":
            validate_guest_results(folder)
        if name == "exofs" and not any(p.is_file() for p in (folder / "guest-output").rglob("*")):
            raise ValueError("missing exofs output")


def finish(directory, status):
    errors = []
    if status == 0:
        try:
            validate(directory)
        except (OSError, ValueError, KeyError, TypeError, StopIteration, ET.ParseError, subprocess.CalledProcessError) as error:
            errors.append(str(error))
            status = 1
    try:
        write(directory / "result.json", {"exit_status": status, "passed": status == 0,
              "finished_at": datetime.now(timezone.utc).isoformat(), "evidence_errors": errors,
              "evidence_files": {str(p.relative_to(directory)): digest(p) for p in retained_files(directory)}})
    except OSError as error:
        print("ERROR Retaining workflow result: " + str(error), file=sys.stderr)
        return status or 1
    for error in errors:
        print("ERROR Workflow evidence: " + error, file=sys.stderr)
    return status


def retained_files(directory):
    """Evidence only: exported source trees, objects and guest images stay out."""
    paths = [p for p in directory.iterdir() if p.is_file() and p.name != "result.json"
             and p.suffix in (".json", ".log")]
    for name in ("stages", "qemu/logs"):
        paths.extend(p for p in (directory / name).rglob("*") if p.is_file())
    return sorted(paths)


def export_evidence(workspace, destination, durable, allow_incomplete=False, run=None):
    destination.mkdir(parents=True, exist_ok=True)
    # run-ci exports only its own invocation; previous failed runs remain
    # available without making a later successful invocation fail validation.
    runs = [run] if run is not None else sorted((workspace / "logs/qemu11-validation").glob("run.*"))
    errors = []
    if not runs and not allow_incomplete:
        errors.append("successful caller has no workflow evidence")
    for run in runs:
        result_path = run / "result.json"
        try:
            result = read(result_path)
            actual = {str(p.relative_to(run)): digest(p) for p in retained_files(run)}
            if result["evidence_files"] != actual:
                errors.append(run.name + ": retained evidence is missing or changed")
            if not allow_incomplete and not result["passed"]:
                errors.append(run.name + ": workflow did not pass")
            if not allow_incomplete:
                checked = subprocess.run(["bash", "-c",
                    'source ./env.sh >/dev/null; '
                    'exec python3 ./qemu-workflow.py --directory "$1" validate',
                    "bash", str(run)],
                    cwd=workspace / "simulator/infra/builder", capture_output=True, text=True)
                if checked.returncode:
                    errors.append(run.name + ": evidence validation failed: " + checked.stderr.strip())
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append(run.name + ": incomplete workflow result: " + str(error))
        # Even a failed/incomplete run must retain the available diagnostics.
        paths = retained_files(run) + ([result_path] if result_path.is_file() else [])
        for path in paths:
            target = destination / run.name / path.relative_to(run)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    inventory = {str(p.relative_to(destination)): digest(p) for p in destination.rglob("*")
                 if p.is_file() and p != destination / "export.json"}
    write(destination / "export.json", {"incomplete_allowed": allow_incomplete, "errors": errors, "files": inventory})
    shutil.copytree(destination, durable, dirs_exist_ok=True)
    for relative, checksum in inventory.items():
        if digest(durable / relative) != checksum:
            raise ValueError("durable evidence copy differs: " + relative)
    print("INFO Durable validation evidence: " + str(durable))
    for error in errors:
        print("WARNING Exported evidence: " + error, file=sys.stderr)
    return 1 if errors and not allow_incomplete else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    commands = parser.add_subparsers(dest="action", required=True)
    stage = commands.add_parser("stage")
    stage.add_argument("name", choices=STAGES)
    stage.add_argument("--stdout")
    stage.add_argument("command", nargs=argparse.REMAINDER)
    commands.add_parser("inputs")
    commands.add_parser("images")
    commands.add_parser("validate")
    launch = commands.add_parser("launch")
    launch.add_argument("--runtime", required=True)
    launch.add_argument("--container", required=True)
    launch.add_argument("--devices", type=int, required=True)
    launch.add_argument("--strategy", type=int, required=True)
    launch.add_argument("--simulator", required=True)
    launch.add_argument("--config", type=Path, required=True)
    end = commands.add_parser("finish")
    end.add_argument("status", type=int)
    export = commands.add_parser("export")
    export.add_argument("--workspace", type=Path, required=True)
    export.add_argument("--run", type=Path, help="export only this workflow invocation")
    export.add_argument("--durable", type=Path, required=True)
    export.add_argument("--allow-incomplete", action="store_true")
    args = parser.parse_args()
    try:
        if args.action == "stage":
            command = args.command[1:] if args.command[:1] == ["--"] else args.command
            return run_stage(args.directory, args.name, command, args.stdout)
        if args.action == "inputs":
            record_inputs(args.directory)
        elif args.action == "images":
            write(args.directory / "container-images.json", images(profile()))
        elif args.action == "launch":
            record_launch(args.directory, json.loads(args.runtime), args.container,
                          args.devices, args.strategy, args.simulator, args.config)
        elif args.action == "validate":
            validate(args.directory)
        elif args.action == "export":
            return export_evidence(args.workspace, args.directory, args.durable, args.allow_incomplete, args.run)
        else:
            return finish(args.directory, args.status)
        return 0
    except (OSError, ValueError, KeyError, TypeError, StopIteration, ET.ParseError, subprocess.CalledProcessError) as error:
        print("ERROR Workflow evidence: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
