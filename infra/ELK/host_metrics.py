#!/usr/bin/env python3
"""ELK metrics of the host simulation tests: characterize, derive bounds, gate.

  host_metrics.py characterize <version> [--runs N]  # writes HOST_TESTS_CASES_METRICS.md
  host_metrics.py derive                            # writes host_tests_bounds.json
  host_metrics.py wipe                              # empty the index before a run someone else starts
  host_metrics.py read                              # print current metrics from elk without run the simulator
  host_metrics.py gate <version>                    # CI: run every case, assert bounds

Each case wipes the filebeat index, runs its tests with background GC disabled
(so the traffic and the GC it triggers are deterministic), waits for ingestion
to settle and reads the metrics over exactly what that case shipped.
The gate also runs the event generator, whose expected counts are its own tally.
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
    "multi_device_tests": "--gtest_filter=*MultiDevice*",
    "ssd_program_compatible_tests": "--ssd_program_compatible_test",
    # the small disk sizes only; run_all_host_tests.sh runs the full suites natively
    "ssd_write_read_tests": "--gtest_filter=DiskSize/WriteReadTest.*/0:DiskSize/WriteReadTest.*/1:"
                            "DiskSize/WriteReadTest.*/2:DiskSize/WriteReadTest.*/3",
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


EVENT_TIME_SAMPLE = 500
EVENT_TIME_TOLERANCE_MS = 1
MISSING_EVENT_TIME_QUERY = {"query": {"bool": {"must_not": {"exists": {"field": "logging_time"}}}}}


def _event_time_query(query):
    return {"size": EVENT_TIME_SAMPLE, "_source": ["@timestamp", "logging_time", "type"], "query": query}


GC_OVER_TIME_QUERY = {  # the "Garbage Collection Events Over Time" panel, as a query
    "size": 0, "track_total_hits": True, "query": _type_filter("GarbageCollectionLog"),
    "aggs": {"over_time": {"date_histogram": {"field": "@timestamp", "fixed_interval": "1s",
                                              "min_doc_count": 1}}}}


def event_time_failures(name, missing, hits, gc_total, gc_buckets):
    """@timestamp must be each event's own logging_time, not ingest time."""
    failures = []
    if gc_total and not gc_buckets:
        failures.append("%s: %d GC events but no @timestamp buckets" % (name, gc_total))
    if missing:
        failures.append("%s: %d events without logging_time" % (name, missing))
    if not hits:
        failures.append("%s: no events sampled for event time (nothing shipped)" % name)
    bad = []
    for hit in hits:
        src = hit["_source"]
        try:
            event = datetime.datetime.strptime(src["logging_time"], LOGGING_TIME_FORMAT).replace(
                tzinfo=datetime.timezone.utc)
            ingest = datetime.datetime.fromisoformat(src["@timestamp"].replace("Z", "+00:00"))
        except (KeyError, ValueError) as e:
            bad.append("%s has no parseable time (%s)" % (src.get("type"), e))
            continue
        if event.year < 2000:
            bad.append("%s logging_time %s is unset" % (src.get("type"), src["logging_time"]))
            continue
        # Elasticsearch keeps @timestamp to the millisecond, logging_time to the microsecond
        event -= datetime.timedelta(microseconds=event.microsecond % 1000)
        skew_ms = abs((ingest - event).total_seconds()) * 1000
        if skew_ms > EVENT_TIME_TOLERANCE_MS:
            bad.append("%s @timestamp %s != logging_time %s" % (src.get("type"), src["@timestamp"],
                                                               src["logging_time"]))
    if bad:
        failures.append("%s: %d of %d sampled events have a wrong event time, first: %s"
                        % (name, len(bad), len(hits), bad[0]))
    return failures


# the generator case's expected values are its own tally, not derived bounds
GENERATOR_CASE = "event_generator"
GC_CASE = "event_generator_gc"
GENERATOR_CASES = {GENERATOR_CASE: "Replay", GC_CASE: "GarbageCollection"}
# under the workspace root, outside the logs folder filebeat reads; run_simulation clears data/ first
GENERATOR_TALLY = "data/event_generator_tally.json"

COUNTS_QUERY = {
    "size": 0,
    "aggs": {"disks": {"terms": {"field": "device_index", "size": 100},
                       "aggs": {"types": {"terms": {"field": "type", "size": 100}}}}},
}


BREAKDOWN_QUERY = {
    "size": 0,
    "aggs": {name: {"terms": {"field": "device_index", "size": 100},
                    "aggs": {"parts": {"terms": {"field": field, "size": 100},
                                       "aggs": {"types": {"terms": {"field": "type", "size": 100}}}}}}
             for name, field in (("channels", "channel"), ("dies", "die"))},
}

CELL_EVENT_TYPES = ["PhysicalCellReadLog", "PhysicalCellProgramLog", "PhysicalCellProgramCompatibleLog",
                    "LogicalCellProgramLog"]
MISSING_DIE_QUERY = {"query": {"bool": {"filter": {"terms": {"type": CELL_EVENT_TYPES}},
                                        "must_not": {"exists": {"field": "die"}}}}}


def missing_die_failures(name, missing):
    line = "%s: %d cell events without die" % (name, missing)
    print("FAIL" if missing else "PASS", line)
    return [line] if missing else []


DIE_SPREAD_CASES = ("sector_tests", "object_tests")
DIE_SPREAD_TYPES = ["PhysicalCellReadLog", "PhysicalCellProgramLog", "LogicalCellProgramLog"]
DIE_SPREAD_QUERY = {"size": 0,
                    "query": {"terms": {"type": DIE_SPREAD_TYPES}},
                    "aggs": {"types": {"terms": {"field": "type", "size": 10},
                                       "aggs": {"dies": {"cardinality": {"field": "die"}}}}}}


def die_spread_failures(name, dies):
    failures = []
    for t in sorted(DIE_SPREAD_TYPES):
        n = dies.get(t, 0)
        line = "%s %s: %d distinct dies" % (name, t, n)
        print("FAIL" if n < 2 else "PASS", line)
        failures += [line] if n < 2 else []
    return failures


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


@dataclasses.dataclass
class ExtendedMetrics:
    reads: int
    writes: int
    erases: int
    ops_per_channel: dict
    register_reads: int
    register_writes: int
    gc_invocations: int
    concurrency_ratio: float
    emulated_time_ms: float


EXTENDED_METRIC_NAMES = [f.name for f in dataclasses.fields(ExtendedMetrics)]

LOGGING_TIME_FORMAT = "%Y-%m-%d_%H-%M-%S.%f"


# logging_time is a fixed-width keyword ("2026-10-05_18-06-45.472875"), so a top_hits
# sort orders it chronologically.
EXTENDED_METRICS_QUERY = {
    "size": 0,
    "aggs": {
        "reads": {"filter": _type_filter("PhysicalCellReadLog")},
        "writes": {"filter": _type_filter("PhysicalCellProgramLog")},
        "erases": {"filter": _type_filter("BlockEraseLog")},
        "register_reads": {"filter": _type_filter("RegisterReadLog")},
        "register_writes": {"filter": _type_filter("RegisterWriteLog")},
        "gcs": {"filter": _type_filter("GarbageCollectionLog")},
        "physical": {
            "filter": {"terms": {"type": ["PhysicalCellReadLog", "PhysicalCellProgramLog", "BlockEraseLog"]}},
            "aggs": {"by_channel": {"terms": {"field": "channel", "size": 64}}},
        },
        "busy_us": {"sum": {"field": "duration_us"}},
        "first_log": {"top_hits": {"size": 1, "sort": [{"logging_time": {"order": "asc"}}],
                                   "_source": {"includes": ["logging_time"]}}},
        "last_log": {"top_hits": {"size": 1, "sort": [{"logging_time": {"order": "desc"}}],
                                  "_source": {"includes": ["logging_time"]}}},
    },
}


def _logging_time_source(aggs, name):
    hits = aggs[name]["hits"]["hits"]
    if not hits:
        raise ElasticError("%s returned no docs" % name)
    value = hits[0]["_source"].get("logging_time")
    if not value:
        raise ElasticError("%s doc carries no logging_time" % name)
    return value


def extended_metrics_from_aggs(aggs):
    """A missing aggregation raises KeyError. An emulated span of 0 reads as a 0 ratio,
    because sum(duration_us) is then meaningless too."""
    first = datetime.datetime.strptime(_logging_time_source(aggs, "first_log"), LOGGING_TIME_FORMAT)
    last = datetime.datetime.strptime(_logging_time_source(aggs, "last_log"), LOGGING_TIME_FORMAT)
    span_us = int(abs(last - first) / datetime.timedelta(microseconds=1))
    channels = {int(bucket["key"]): bucket["doc_count"]
                for bucket in aggs["physical"]["by_channel"]["buckets"]}
    return ExtendedMetrics(
        reads=aggs["reads"]["doc_count"],
        writes=aggs["writes"]["doc_count"],
        erases=aggs["erases"]["doc_count"],
        ops_per_channel=dict(sorted(channels.items())),
        register_reads=aggs["register_reads"]["doc_count"],
        register_writes=aggs["register_writes"]["doc_count"],
        gc_invocations=aggs["gcs"]["doc_count"],
        concurrency_ratio=_ratio(aggs["busy_us"]["value"], span_us),
        emulated_time_ms=span_us / 1000.0,
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

    def extended_metrics(self):
        return extended_metrics_from_aggs(
            self.request("POST", INDEX + "/_search", EXTENDED_METRICS_QUERY)["aggregations"])

    def missing_die(self):
        return self.request("POST", INDEX + "/_count", MISSING_DIE_QUERY)["count"]

    def dies_per_type(self):
        aggs = self.request("POST", INDEX + "/_search", DIE_SPREAD_QUERY)["aggregations"]
        return {b["key"]: b["dies"]["value"] for b in aggs["types"]["buckets"]}

    def event_times(self):
        """(events without logging_time, sampled hits incl. GC-only and sync-only, GC total, GC buckets)"""
        missing = self.request("POST", INDEX + "/_count", MISSING_EVENT_TIME_QUERY)["count"]
        hits = []
        for query in ({"match_all": {}}, _type_filter("GarbageCollectionLog"), _type_filter("LoggeingServerSync")):
            hits += self.request("POST", INDEX + "/_search", _event_time_query(query))["hits"]["hits"]
        gc = self.request("POST", INDEX + "/_search", GC_OVER_TIME_QUERY)
        return missing, hits, gc["hits"]["total"]["value"], len(gc["aggregations"]["over_time"]["buckets"])


def run_simulation(version, args, env=None):
    """Run one case in the test container. data/ is cleared first so leftover images can't skew it."""
    cmd = ("rm -rf data/* && mkdir -p data/0 data/1 data/2 && "
           "./simulation_tests_main --ci --no-bg-gc " + args)
    proc = subprocess.run(
        ["bash", "-c", 'source ./builder.sh && evssim_run_at_folder "$1" '
                       '"$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/simulation" "$2"', "bash", version, cmd],
        cwd=BUILDER_DIR, env=dict(os.environ, **(env or {})),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
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


def print_metrics(names, metrics):
    """One line per metric, so a caller can parse `    <name> = <value>` with awk/cut."""
    for name in names:
        print("    %-22s = %r" % (name, getattr(metrics, name)))


def counts_from_aggs(aggs):
    """{disk: {event type: count}} from COUNTS_QUERY, keys as strings like the tally's."""
    return {str(d["key"]): {t["key"]: t["doc_count"] for t in d["types"]["buckets"]}
            for d in aggs["disks"]["buckets"]}


def breakdown_from_aggs(aggs):
    """{"<disk> channel <c>" or "<disk> die <d>": {event type: count}} from BREAKDOWN_QUERY, the tally's keys."""
    return {"%s %s %s" % (disk["key"], label, part["key"]): {t["key"]: t["doc_count"] for t in part["types"]["buckets"]}
            for agg, label in (("channels", "channel"), ("dies", "die"))
            for disk in aggs[agg]["buckets"] for part in disk["parts"]["buckets"]}


def check_counts(tally, counts, name=GENERATOR_CASE):
    """Every key (a disk, "<disk> channel <c>" or "<disk> die <d>") and event type in either side must match
    exactly; prints a PASS/FAIL line for each."""
    failures = []
    for disk in sorted(set(tally) | set(counts)):
        want, got = tally.get(disk, {}), counts.get(disk, {})
        for event_type in sorted(set(want) | set(got)):
            line = "%s disk %s %s: expected %d, got %d" % (
                name, disk, event_type, want.get(event_type, 0), got.get(event_type, 0))
            ok = want.get(event_type, 0) == got.get(event_type, 0)
            print("PASS" if ok else "FAIL", line)
            if not ok:
                failures.append(line)
    return failures


def generator_gate(es, version, name, test):
    """Run one EventGeneratorTest and assert ES counts equal the tally it wrote, per disk and event type;
    the Replay case also per (disk, channel) and per (disk, die)."""
    print("[host_metrics] %s: wipe, run, settle" % name, flush=True)
    es.wipe()
    run_simulation(version, "--gtest_filter=EventGeneratorTest." + test,
                   {"EVSSIM_GENERATOR_TALLY": os.path.join(os.environ["EVSSIM_DOCKER_ROOT_PATH"], GENERATOR_TALLY)})
    print("[host_metrics] %s: settled at %d docs" % (name, es.settle()), flush=True)
    with open(os.path.join(os.environ["EVSSIM_ROOT_PATH"], GENERATOR_TALLY)) as fh:
        tally = json.load(fh)
    counts = counts_from_aggs(es.request("POST", INDEX + "/_search", COUNTS_QUERY)["aggregations"])
    if name == GENERATOR_CASE:
        counts.update(breakdown_from_aggs(es.request("POST", INDEX + "/_search", BREAKDOWN_QUERY)["aggregations"]))
    return check_counts(tally, counts, name) + missing_die_failures(name, es.missing_die())


def gate(version):
    with open(BOUNDS_JSON) as fh:  # missing file raises: no silent report-only mode
        bounds = json.load(fh)
    es = Elastic(os.environ.get("ELASTIC_PASSWORD", "changeme"))
    failures = []
    for name in CASES:  # sequential by construction
        metrics = measure_case(es, version, name)
        print_metrics(METRIC_NAMES, metrics)
        failures += check_bounds(name, metrics, bounds.get(name, {}))
        failures += event_time_failures(name, *es.event_times())
        failures += missing_die_failures(name, es.missing_die())
        if name in DIE_SPREAD_CASES:
            failures += die_spread_failures(name, es.dies_per_type())
    for name, test in GENERATOR_CASES.items():
        failures += generator_gate(es, version, name, test)
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


def bounds_for(values):
    """Every run must agree: the gate is exact, so a metric that varied has no bound."""
    if min(values) != max(values):
        raise ValueError("varied over [%r, %r]" % (min(values), max(values)))
    return {"min": _num(values[0]), "max": _num(values[0])}


def _num(x):
    return int(x) if float(x).is_integer() else x


def derive():
    with open(METRICS_MD) as fh:
        data = parse_metrics_md(fh.read())
    bounds, varied = {}, []
    for case, metrics in data.items():
        bounds[case] = {}
        for metric in METRIC_NAMES:
            try:
                bounds[case][metric] = bounds_for(metrics[metric])  # a metric missing from the file raises
            except ValueError as e:
                varied.append("%s.%s %s" % (case, metric, e))
    if varied:
        for v in varied:
            print("NON-DETERMINISTIC", v)
        print("[host_metrics] not writing %s: every metric must be identical across runs" % BOUNDS_JSON)
        return 1
    with open(BOUNDS_JSON, "w") as fh:
        json.dump(bounds, fh, indent=2, sort_keys=True)
        fh.write("\n")
    print("[host_metrics] wrote", BOUNDS_JSON)
    return 0


def wipe():
    """Empty the index and require it to stay empty, for a run this process does not start."""
    es = Elastic(os.environ.get("ELASTIC_PASSWORD", "changeme"))
    print("[host_metrics]: wipe", flush=True)
    es.wipe()
    print("[host_metrics]: wipe all indexes successfully", flush=True)
    return 0


def read(extended=False):
    """Wait for ingestion of a run this process did not start, then print its metrics."""
    es = Elastic(os.environ.get("ELASTIC_PASSWORD", "changeme"))
    print("[host_metrics]: settle", flush=True)
    print("[host_metrics]: settled at %d docs" % es.settle(), flush=True)
    if extended:
        print_metrics(EXTENDED_METRIC_NAMES, es.extended_metrics())
    else:
        print_metrics(METRIC_NAMES, es.metrics())
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("characterize")
    p.add_argument("version")
    p.add_argument("--runs", type=int, default=5)
    sub.add_parser("derive")
    sub.add_parser("wipe")
    sub.add_parser("read").add_argument("--extended", action="store_true", default=False)
    sub.add_parser("gate").add_argument("version")
    args = parser.parse_args(argv)
    if args.cmd == "characterize":
        return characterize(args.version, args.runs)
    if args.cmd == "derive":
        return derive()
    if args.cmd == "wipe":
        return wipe()
    if args.cmd == "read":
        return read(args.extended)
    return gate(args.version)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
