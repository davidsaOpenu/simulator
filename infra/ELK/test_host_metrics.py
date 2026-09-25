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
    def test_zero_spread_is_exact(self):
        self.assertEqual({"min": 17896.0, "max": 17896.0}, hm.bounds_for("write_count", [17896.0] * 5))

    def test_spread_widens_by_range(self):
        self.assertEqual({"min": 90.0, "max": 120.0}, hm.bounds_for("write_count", [100.0, 110.0]))

    def test_never_below_zero(self):
        self.assertEqual(0.0, hm.bounds_for("gc_invocations", [1.0, 5.0])["min"])

    def test_wa_clamped_at_one(self):
        self.assertEqual(1.0, hm.bounds_for("write_amplification", [1.0, 1.1])["min"])

    def test_wa_zero_without_logical_writes_stays_exact(self):
        self.assertEqual({"min": 0.0, "max": 0.0}, hm.bounds_for("write_amplification", [0.0, 0.0]))

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
