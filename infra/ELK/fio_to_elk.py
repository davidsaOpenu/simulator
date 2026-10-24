#!/usr/bin/env python3
"""Turn the fio JSON results of the filesystem benchmark into ELK log documents.

<results_dir> holds one fio-<filesystem>.json per benchmarked filesystem, as
docker-test-filesystems.sh copies them from the guest. Every job of every file
becomes one JSON line of type FioBenchmarkLog in FILE.
"""
import argparse
import configparser
import datetime
import glob
import json
import os
import sys
import tempfile
import uuid
import zoneinfo
from typing import Literal, TypedDict

DOC_TYPE = "FioBenchmarkLog"
VARIANT_ENV = "EVSSIM_BENCHMARK_VARIANT"
DEFAULT_VARIANT = "baseline"
RESULT_GLOB = "fio-*.json"

# Must match the timestamp processor in filebeat.yml
LOGGING_TIME_ZONE = "Asia/Jerusalem"
LOGGING_TIME_FORMAT = "%Y-%m-%d %H:%M:%S.%f"

# The [nvme0N] section of docker/ssd.conf.filesystems.template each filesystem
# runs on. Must match FILESYSTEMS in tests/guest/filesystems/fs_common.py.
CONFIG_IDS = {"exofs": 1, "ext4": 2}

Operation = Literal["read", "write"]

RW_OPERATIONS: dict[str, Operation] = {
    "read": "read", "randread": "read", "write": "write", "randwrite": "write",
}
SIZE_UNITS = {"": 1, "k": 1 << 10, "m": 1 << 20, "g": 1 << 30}

JobOptions = dict[str, str | None]


class FioStats(TypedDict):
    io_bytes: int
    iops: float
    bw: int


class FioJob(TypedDict):
    jobname: str
    read: FioStats
    write: FioStats


FioReport = TypedDict("FioReport", {"fio version": str, "jobs": list[FioJob]})


class RunInfo(TypedDict):
    type: str
    logging_time: str
    filesystem: str
    variant: str
    run_id: str
    kernel_version: str
    config_id: int


class Document(RunInfo):
    fio_version: str
    job: str
    block_size: str
    block_size_bytes: int
    operation: Operation
    iops: float
    bw_kbps: float


class ConversionError(Exception):
    pass


def read_job_file(path: str) -> dict[str, JobOptions]:
    """fio 2.1 (the guest's) does not report bs or rw in its JSON."""
    parser = configparser.ConfigParser(allow_no_value=True, interpolation=None,
                                       inline_comment_prefixes=(";", "#"))
    try:
        with open(path) as fh:
            parser.read_file(fh)
    except (OSError, configparser.Error) as e:
        raise ConversionError("cannot read fio job file %s: %s" % (path, e)) from e
    global_options = dict(parser["global"]) if parser.has_section("global") else {}
    return {name: dict(global_options, **parser[name])
            for name in parser.sections() if name != "global"}


def parse_size(bs: str) -> int:
    number, unit = (bs[:-1], bs[-1].lower()) if bs[-1:].isalpha() else (bs, "")
    if not number.isdigit() or unit not in SIZE_UNITS:
        raise ConversionError("unsupported block size %r" % bs)
    return int(number) * SIZE_UNITS[unit]


def job_document(job: FioJob, job_file_options: dict[str, JobOptions],
                 run: RunInfo, fio_version: str) -> Document:
    name = job["jobname"]
    if name not in job_file_options:
        raise ConversionError("job %r is not in the fio job file" % name)
    bs = job_file_options[name].get("bs") or ""
    rw = job_file_options[name].get("rw") or ""
    if not bs:
        raise ConversionError("job %s has no bs in the job file" % name)
    if rw not in RW_OPERATIONS:
        raise ConversionError("job %s: unsupported rw=%s" % (name, rw))
    operation = RW_OPERATIONS[rw]
    stats = job[operation]
    if not stats["io_bytes"]:
        raise ConversionError("job %s did no %s I/O" % (name, operation))
    return {**run, "fio_version": fio_version, "job": name, "block_size": bs,
            "block_size_bytes": parse_size(bs), "operation": operation,
            "iops": float(stats["iops"]), "bw_kbps": float(stats["bw"])}


def report_documents(path: str, job_file_options: dict[str, JobOptions],
                     run: RunInfo) -> list[Document]:
    try:
        with open(path) as fh:
            report: FioReport = json.load(fh)
    except (OSError, ValueError) as e:
        raise ConversionError("cannot read fio JSON %s: %s" % (path, e)) from e
    fio_version = report.get("fio version", "unknown")
    return [job_document(job, job_file_options, run, fio_version) for job in report["jobs"]]


def convert(results_dir: str, job_file: str, kernel_version: str, variant: str,
            run_id: str) -> list[Document]:
    paths = sorted(glob.glob(os.path.join(results_dir, RESULT_GLOB)))
    if not paths:
        raise ConversionError("no %s in %s" % (RESULT_GLOB, results_dir))
    if not kernel_version:
        raise ConversionError("kernel version is empty")
    job_file_options = read_job_file(job_file)
    now = datetime.datetime.now(zoneinfo.ZoneInfo(LOGGING_TIME_ZONE))
    stamp = now.strftime(LOGGING_TIME_FORMAT)[:-3]
    docs: list[Document] = []
    for path in paths:
        filesystem = os.path.basename(path)[len("fio-"):-len(".json")]
        if filesystem not in CONFIG_IDS:
            raise ConversionError("%s: unknown filesystem %r, expected one of: %s"
                                  % (path, filesystem, ", ".join(sorted(CONFIG_IDS))))
        run: RunInfo = {
            "type": DOC_TYPE,
            "logging_time": stamp,
            "filesystem": filesystem,
            "variant": variant,
            "run_id": run_id,
            "kernel_version": kernel_version,
            "config_id": CONFIG_IDS[filesystem],
        }
        docs += report_documents(path, job_file_options, run)
    return docs


def write_documents(docs: list[Document], output: str) -> None:
    """Write to a hidden temp file first, so filebeat never reads a partial file."""
    directory = os.path.dirname(os.path.abspath(output))
    fd, tmp = tempfile.mkstemp(prefix=".fio_to_elk-", dir=directory)
    try:
        with os.fdopen(fd, "w") as fh:
            for doc in docs:
                fh.write(json.dumps(doc) + "\n")
        os.chmod(tmp, 0o644)
        os.replace(tmp, output)
    except BaseException:
        os.unlink(tmp)
        raise


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("results_dir")
    parser.add_argument("--job-file", required=True)
    parser.add_argument("--kernel-version", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    variant = os.environ.get(VARIANT_ENV, "").strip() or DEFAULT_VARIANT
    run_id = str(uuid.uuid4())
    try:
        docs = convert(args.results_dir, args.job_file, args.kernel_version.strip(), variant, run_id)
        write_documents(docs, args.output)
    except (ConversionError, OSError) as e:
        print("[fio_to_elk] ERROR %s" % e, file=sys.stderr)
        return 1
    print("[fio_to_elk] run_id=%s variant=%s: wrote %d docs to %s"
          % (run_id, variant, len(docs), args.output))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
