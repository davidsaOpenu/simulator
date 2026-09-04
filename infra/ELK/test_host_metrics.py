import io
import json
import unittest
import urllib.error
from unittest import mock

import host_metrics as hm


def metrics(**overrides):
    values = dict(write_count=10, logical_write_count=8, read_iops=1.5, write_iops=2.5,
                  write_amplification=1.25, read_speed_mbps=0.5, write_speed_mbps=0.75,
                  gc_invocations=3, disk_utilization=0.4)
    values.update(overrides)
    return hm.Metrics(**values)


def exact_bounds(m):
    return {name: {"min": getattr(m, name), "max": getattr(m, name)} for name in hm.METRIC_NAMES}


class CheckBoundsTest(unittest.TestCase):
    def test_exact_match_passes(self):
        m = metrics()
        self.assertEqual([], hm.check_bounds("c", m, exact_bounds(m)))

    def test_out_of_bounds_fails(self):
        b = exact_bounds(metrics())
        failures = hm.check_bounds("c", metrics(write_count=11), b)
        self.assertEqual(1, len(failures))
        self.assertIn("c.write_count=11 not in", failures[0])

    def test_missing_bound_fails(self):
        b = exact_bounds(metrics())
        del b["read_iops"]
        self.assertEqual(["c.read_iops=1.5 has no bound"], hm.check_bounds("c", metrics(), b))

    def test_missing_case_fails_every_metric(self):
        self.assertEqual(len(hm.METRIC_NAMES), len(hm.check_bounds("c", metrics(), {})))


class DeriveTest(unittest.TestCase):
    def test_identical_runs_give_exact_bound(self):
        self.assertEqual({"min": 17896, "max": 17896}, hm.bounds_for([17896.0] * 5))

    def test_float_bound_stays_float(self):
        self.assertEqual({"min": 1.25, "max": 1.25}, hm.bounds_for([1.25, 1.25]))

    def test_varied_metric_is_rejected(self):
        with self.assertRaises(ValueError):
            hm.bounds_for([100.0, 110.0])

    def test_parse_round_trips_floats(self):
        v = 1 / 3
        text = "# header\n\ntest case: a\nrun #1\nread_iops=%r\nrun #2\nread_iops=%r\n" % (v, v)
        self.assertEqual({"a": {"read_iops": [v, v]}}, hm.parse_metrics_md(text))


class MetricsFromAggsTest(unittest.TestCase):
    AGGS = {"reads": {"doc_count": 100}, "writes": {"doc_count": 300}, "logical_writes": {"doc_count": 200},
            "gcs": {"doc_count": 4}, "util": {"avg": {"value": 0.5}}, "ssd_size": {"value": 4096.0},
            "total_pages": {"value": 1.0}, "sim_us": {"value": 2e6}}

    def test_formulas(self):
        m = hm.metrics_from_aggs(self.AGGS)
        self.assertEqual((300, 200, 4, 0.5), (m.write_count, m.logical_write_count, m.gc_invocations,
                                              m.disk_utilization))
        self.assertEqual((50.0, 150.0, 1.5), (m.read_iops, m.write_iops, m.write_amplification))
        self.assertEqual(100 * 4096 * 8 / 2e6, m.read_speed_mbps)

    def test_missing_aggregation_raises(self):
        aggs = dict(self.AGGS)
        del aggs["gcs"]
        with self.assertRaises(KeyError):
            hm.metrics_from_aggs(aggs)


def hit(logging_time, timestamp, event_type="GarbageCollectionLog"):
    src = {"type": event_type, "@timestamp": timestamp}
    if logging_time is not None:
        src["logging_time"] = logging_time
    return {"_source": src}


class EventTimeTest(unittest.TestCase):
    def test_event_time_parsed_passes(self):
        hits = [hit("2026-10-02_09-15-30.123456", "2026-10-02T09:15:30.123Z")]
        self.assertEqual([], hm.event_time_failures("c", 0, hits, 0, 0))

    def test_ingest_time_fails(self):
        hits = [hit("2026-10-02_09-15-30.123456", "2026-10-02T09:15:30.123Z"),
                hit("2026-10-02_09-15-30.123456", "2026-10-02T12:15:31.000Z")]
        failures = hm.event_time_failures("c", 0, hits, 0, 0)
        self.assertEqual(1, len(failures))
        self.assertIn("1 of 2 sampled events", failures[0])

    def test_missing_logging_time_fails(self):
        self.assertIn("c: 3 events without logging_time", hm.event_time_failures("c", 3, [hit(None, "x")], 0, 0))

    def test_unset_time_fails(self):
        failures = hm.event_time_failures("c", 0, [hit("1970-01-01_00-00-00.000000", "1970-01-01T00:00:00.000Z")], 0, 0)
        self.assertIn("is unset", failures[0])

    def test_gc_without_time_buckets_fails(self):
        hits = [hit("2026-10-02_09-15-30.123456", "2026-10-02T09:15:30.123Z")]
        self.assertEqual(["c: 96 GC events but no @timestamp buckets"], hm.event_time_failures("c", 0, hits, 96, 0))
        self.assertEqual([], hm.event_time_failures("c", 0, hits, 96, 3))

    def test_nothing_shipped_fails(self):
        self.assertEqual(1, len(hm.event_time_failures("c", 0, [], 0, 0)))


class ElasticTest(unittest.TestCase):
    def setUp(self):
        self.es = hm.Elastic("pw")

    def respond(self, body):
        return mock.patch.object(self.es.opener, "open", return_value=io.BytesIO(json.dumps(body).encode()))

    def test_es_error_body_raises(self):
        with self.respond({"error": {"type": "search_phase_execution_exception"}}):
            with self.assertRaises(hm.ElasticError):
                self.es.request("POST", "x/_search", {})

    def test_http_error_raises(self):
        err = urllib.error.HTTPError("https://es/x", 401, "Unauthorized", {}, io.BytesIO(b"denied"))
        with mock.patch.object(self.es.opener, "open", side_effect=err):
            with self.assertRaises(hm.ElasticError):
                self.es.request("GET", "x")

    def test_404_allowed_only_when_asked(self):
        def err(*_a, **_k):
            raise urllib.error.HTTPError("https://es/x", 404, "Not Found", {}, io.BytesIO(b""))
        with mock.patch.object(self.es.opener, "open", side_effect=err):
            self.assertEqual({}, self.es.request("DELETE", "x", allow_404=True))
            with self.assertRaises(hm.ElasticError):
                self.es.request("DELETE", "x")

    def test_metrics_with_es_error_raises(self):
        with self.respond({"error": "boom"}):
            with self.assertRaises(hm.ElasticError):
                self.es.metrics()


if __name__ == "__main__":
    unittest.main()
