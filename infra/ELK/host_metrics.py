#!/usr/bin/env python3
"""ELK metrics of the host simulation tests: characterize, derive bounds, gate.

  host_metrics.py characterize <version> [--runs N]  # writes HOST_TESTS_CASES_METRICS.md
  host_metrics.py derive                            # writes host_tests_bounds.json
  host_metrics.py gate <version>                    # CI: run every case, assert bounds

Each case wipes the filebeat index, runs its tests with background GC disabled
(so the traffic and the GC it triggers are deterministic), waits for ingestion
to settle and reads the metrics over exactly what that case shipped.
Any error fails the run; the only way to pass is for every check to pass.
"""
import argparse
import base64
import dataclasses
import datetime
import json
import os
import ssl
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
BUILDER_DIR = os.path.join(HERE, "..", "builder")
METRICS_MD = os.path.join(HERE, "..", "..", "eVSSIM", "docs", "HOST_TESTS_CASES_METRICS.md")
BOUNDS_JSON = os.path.join(HERE, "host_tests_bounds.json")
ES_URL = "https://localhost:9200"
INDEX = "filebeat-*"

POLL_SECS = 5
STABLE_POLLS = 13
TIMEOUT_SECS = 900

CASES = {
    "sector_tests": "--sector-tests",
    "object_tests": "--object-tests",
    "ssd_io_emulator_tests": "--ssd-io-emulator-tests",
    "multi_device_tests": "--multi_device_tests",
    "ssd_program_compatible_tests": "--ssd_program_compatible_test",
    # the small disk sizes only; run_all_host_tests.sh runs the full suites natively
    "ssd_write_read_tests": "--gtest_filter=*WriteReadTest*/0:*WriteReadTest*/1:*WriteReadTest*/2:*WriteReadTest*/3",
    "onfi_ops_tests": "--gtest_filter=*OnfiCommandsTest*/1-*PageProgramAllSuccess*:*ReadAllSuccess*",
}


@dataclasses.dataclass
class Metrics:
    write_count: int
    logical_write_count: int
    read_iops: float
    write_iops: float
    write_amplification: float
    read_speed_mbps: float
    write_speed_mbps: float
    gc_invocations: int
    disk_utilization: float


METRIC_NAMES = [f.name for f in dataclasses.fields(Metrics)]


def _type_filter(event_type):
    return {"term": {"type": event_type}}


METRICS_QUERY = {
    "size": 0,
    "aggs": {
        "reads": {"filter": _type_filter("PhysicalCellReadLog")},
        "writes": {"filter": _type_filter("PhysicalCellProgramLog")},
        "logical_writes": {"filter": _type_filter("LogicalCellProgramLog")},
        "gcs": {"filter": _type_filter("GarbageCollectionLog")},
        "util": {"filter": _type_filter("SsdUtilizationLog"),
                 "aggs": {"avg": {"avg": {"field": "utilization_percent"}}}},
        "ssd_size": {"max": {"field": "test.ssd.size"}},
        "total_pages": {"max": {"field": "total_pages"}},
        "sim_us": {"sum": {"field": "duration_us"}},
    },
}


def _ratio(num, den):
    return num / den if den else 0.0


def metrics_from_aggs(aggs):
    """A missing aggregation raises KeyError. A field no event carried (e.g. no utilization
    events) reads as 0; the exact bounds still catch it if a case that had them loses them."""
    reads = aggs["reads"]["doc_count"]
    writes = aggs["writes"]["doc_count"]
    logical = aggs["logical_writes"]["doc_count"]
    secs = aggs["sim_us"]["value"] / 1e6
    page_size = round(_ratio(aggs["ssd_size"]["value"] or 0, aggs["total_pages"]["value"] or 0))
    return Metrics(
        write_count=writes,
        logical_write_count=logical,
        read_iops=_ratio(reads, secs),
        write_iops=_ratio(writes, secs),
        write_amplification=_ratio(writes, logical),
        read_speed_mbps=_ratio(reads * page_size * 8, secs * 1e6),
        write_speed_mbps=_ratio(writes * page_size * 8, secs * 1e6),
        gc_invocations=aggs["gcs"]["doc_count"],
        disk_utilization=aggs["util"]["avg"]["value"] or 0.0,
    )


class ElasticError(Exception):
    pass


class Elastic:
    def __init__(self, password):
        self.auth = "Basic " + base64.b64encode(("elastic:" + password).encode()).decode()
        # self-signed cert; no proxy, ES runs on this host
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({}), urllib.request.HTTPSHandler(context=context))

    def request(self, method, path, body=None, allow_404=False):
        """Any HTTP error or ES 'error' field raises; nothing is silently empty."""
        req = urllib.request.Request(
            ES_URL + "/" + path, method=method,
            data=None if body is None else json.dumps(body).encode(),
            headers={"Authorization": self.auth, "Content-Type": "application/json"})
        try:
            with self.opener.open(req, timeout=60) as resp:
                result = json.load(resp)
        except urllib.error.HTTPError as e:
            if allow_404 and e.code == 404:
                return {}
            raise ElasticError("%s %s: HTTP %d %s" % (method, path, e.code, e.read()[:300])) from e
        except urllib.error.URLError as e:
            raise ElasticError("%s %s: %s" % (method, path, e.reason)) from e
        if "error" in result:
            raise ElasticError("%s %s: %s" % (method, path, result["error"]))
        return result

    def count(self):
        # right after a wipe filebeat recreates the data stream and its shards can still be
        # initializing, which answers _count with search_phase_execution_exception
        for attempt in range(5):
            try:
                return self.request("GET", INDEX + "/_count")["count"]
            except ElasticError:
                if attempt == 4:
                    raise
                time.sleep(2)

    def wipe(self):
        """Delete, then require the index to stay empty: filebeat retries can land late."""
        self.request("DELETE", "_data_stream/" + INDEX, allow_404=True)
        quiet_since = time.monotonic()
        deadline = quiet_since + TIMEOUT_SECS
        while time.monotonic() < deadline:
            if self.count():
                self.request("DELETE", "_data_stream/" + INDEX, allow_404=True)
                quiet_since = time.monotonic()
            elif time.monotonic() - quiet_since >= STABLE_POLLS * POLL_SECS:
                return
            time.sleep(POLL_SECS)
        raise ElasticError("stragglers kept arriving for %ds after the wipe" % TIMEOUT_SECS)

    def settle(self):
        """Wait until the doc count is non-zero and unchanged for STABLE_POLLS polls."""
        prev, stable = -1, 0
        deadline = time.monotonic() + TIMEOUT_SECS
        while time.monotonic() < deadline:
            c = self.count()
            stable = stable + 1 if c > 0 and c == prev else 0
            if stable >= STABLE_POLLS:
                return c
            prev = c
            time.sleep(POLL_SECS)
        raise ElasticError("ingestion did not settle in %ds, last count=%d (0 means nothing shipped)"
                           % (TIMEOUT_SECS, prev))

    def metrics(self):
        return metrics_from_aggs(self.request("POST", INDEX + "/_search", METRICS_QUERY)["aggregations"])


def run_simulation(version, args):
    """Run one case in the test container. data/ is cleared first so leftover images can't skew it."""
    cmd = ("rm -rf data/* && mkdir -p data/0 data/1 data/2 && "
           "./simulation_tests_main --ci --no-bg-gc " + args)
    proc = subprocess.run(
        ["bash", "-c", 'source ./builder.sh && evssim_run_at_folder "$1" '
                       '"$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/simulation" "$2"', "bash", version, cmd],
        cwd=BUILDER_DIR, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.returncode:
        print("\n".join(proc.stdout.splitlines()[-25:]), file=sys.stderr)
        raise subprocess.CalledProcessError(proc.returncode, args)


def measure_case(es, version, name):
    print("[host_metrics] %s: wipe, run, settle" % name, flush=True)
    es.wipe()
    run_simulation(version, CASES[name])
    print("[host_metrics] %s: settled at %d docs" % (name, es.settle()), flush=True)
    return es.metrics()


def check_bounds(name, metrics, case_bounds):
    """Every metric must have a bound and be inside it; a missing one fails."""
    failures = []
    for metric in METRIC_NAMES:
        value = getattr(metrics, metric)
        b = case_bounds.get(metric)
        if b is None:
            failures.append("%s.%s=%r has no bound" % (name, metric, value))
        elif not b["min"] <= value <= b["max"]:
            failures.append("%s.%s=%r not in [%r, %r]" % (name, metric, value, b["min"], b["max"]))
    return failures


def gate(version):
    with open(BOUNDS_JSON) as fh:  # missing file raises: no silent report-only mode
        bounds = json.load(fh)
    es = Elastic(os.environ.get("ELASTIC_PASSWORD", "changeme"))
    failures = []
    for name in CASES:  # sequential by construction
        metrics = measure_case(es, version, name)
        for metric in METRIC_NAMES:
            print("    %-22s = %r" % (metric, getattr(metrics, metric)))
        failures += check_bounds(name, metrics, bounds.get(name, {}))
    for f in failures:
        print("FAIL", f)
    print("[host_metrics] RESULT:", "FAIL" if failures else "PASS")
    return 1 if failures else 0


def characterize(version, runs):
    """Run every case `runs` times, rotating the case order each round, and write METRICS_MD."""
    es = Elastic(os.environ.get("ELASTIC_PASSWORD", "changeme"))
    names = list(CASES)
    results = {name: [] for name in names}
    orders = []
    for r in range(runs):
        order = names[r % len(names):] + names[:r % len(names)]
        orders.append(order)
        for name in order:
            results[name].append(measure_case(es, version, name))
    lines = ["# HOST_TESTS_CASES_METRICS (generated %s by infra/ELK/host_metrics.py characterize)"
             % datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
             "# %d runs per case with background GC disabled; case order rotated each round:" % runs]
    lines += ["# run #%d order: %s" % (i + 1, " ".join(o)) for i, o in enumerate(orders)]
    for name in names:
        lines += ["", "test case: %s" % name]
        for i, m in enumerate(results[name]):
            lines.append("run #%d" % (i + 1))
            lines += ["%s=%r" % (metric, getattr(m, metric)) for metric in METRIC_NAMES]
    with open(METRICS_MD, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("[host_metrics] wrote", METRICS_MD)
    return 0


def parse_metrics_md(text):
    """{case: {metric: [value per run]}} from the characterization file."""
    data, case = {}, None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("test case:"):
            case = data.setdefault(line.split(":", 1)[1].strip(), {})
        elif case is not None and "=" in line and not line.startswith("#"):
            key, _, val = line.partition("=")
            case.setdefault(key, []).append(float(val))
    return data


def bounds_for(metric, values):
    """Zero spread gives an exact gate; otherwise widen by the observed range on each side."""
    lo, hi = min(values), max(values)
    spread = hi - lo
    lo, hi = max(0.0, lo - spread), hi + spread
    if metric == "write_amplification" and min(values) >= 1.0:
        lo = max(lo, 1.0)  # WA below 1 is physically impossible
    return {"min": lo, "max": hi}


def derive():
    with open(METRICS_MD) as fh:
        data = parse_metrics_md(fh.read())
    bounds, varied = {}, []
    for case, metrics in data.items():
        bounds[case] = {}
        for metric in METRIC_NAMES:
            values = metrics[metric]  # a metric missing from the file raises
            bounds[case][metric] = bounds_for(metric, values)
            if min(values) != max(values):
                varied.append("%s.%s varied over [%r, %r]" % (case, metric, min(values), max(values)))
    with open(BOUNDS_JSON, "w") as fh:
        json.dump(bounds, fh, indent=2, sort_keys=True)
        fh.write("\n")
    print("[host_metrics] wrote", BOUNDS_JSON)
    for v in varied:
        print("NON-DETERMINISTIC", v)
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("characterize")
    p.add_argument("version")
    p.add_argument("--runs", type=int, default=5)
    sub.add_parser("derive")
    sub.add_parser("gate").add_argument("version")
    args = parser.parse_args(argv)
    if args.cmd == "characterize":
        return characterize(args.version, args.runs)
    if args.cmd == "derive":
        return derive()
    return gate(args.version)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
