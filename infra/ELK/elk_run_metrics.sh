#!/bin/bash

ES_URL="${ES_URL:-https://localhost:9200}"
INDEX_PATTERN="${INDEX_PATTERN:-filebeat-*}"
SETTLE_TIMEOUT_SECS="${SETTLE_TIMEOUT_SECS:-900}"
SETTLE_INTERVAL_SECS="${SETTLE_INTERVAL_SECS:-5}"
SETTLE_STABLE_POLLS="${SETTLE_STABLE_POLLS:-13}"

for _t in curl python3; do
  command -v "$_t" >/dev/null 2>&1 || {
    echo "[elk_run_metrics] FATAL: '$_t' is required but not found in PATH" >&2
    return 1 2>/dev/null || exit 1
  }
done

_json_get() {
  python3 -c '
import sys, json
default = sys.argv[2]
try:
    cur = json.load(sys.stdin)
except Exception:
    print(default); sys.exit(0)
for key in (sys.argv[1].split(".") if sys.argv[1] else []):
    if isinstance(cur, dict) and cur.get(key) is not None:
        cur = cur[key]
    else:
        print(default); sys.exit(0)
print(cur if cur is not None else default)
' "$1" "${2-}"
}

_es() {
  local path="$1"; shift
  curl -k -s -u "elastic:${ELASTIC_PASSWORD:-changeme}" \
    -H 'Content-Type: application/json' "$ES_URL/$path" "$@"
}

elk_doc_count() {
  _es "$INDEX_PATTERN/_count" | _json_get count 0
}

elk_wipe() {
  local quiet_needed=$((SETTLE_STABLE_POLLS * SETTLE_INTERVAL_SECS))
  local deadline=$((SECONDS + SETTLE_TIMEOUT_SECS)) quiet_since c
  _es "_data_stream/$INDEX_PATTERN" -X DELETE >/dev/null
  _es "$INDEX_PATTERN?expand_wildcards=all" -X DELETE >/dev/null
  quiet_since=$SECONDS
  echo "[elk_run_metrics] index wiped; verifying it stays empty for ${quiet_needed}s (filebeat retry backoff can exceed a short check)"
  while (( SECONDS < deadline )); do
    c="$(elk_doc_count)"
    if [[ "$c" != "0" ]]; then
      _es "_data_stream/$INDEX_PATTERN" -X DELETE >/dev/null
      _es "$INDEX_PATTERN?expand_wildcards=all" -X DELETE >/dev/null
      quiet_since=$SECONDS
    elif (( SECONDS - quiet_since >= quiet_needed )); then
      echo "[elk_run_metrics] index wiped ($INDEX_PATTERN empty and quiet for ${quiet_needed}s)"
      return 0
    fi
    sleep "$SETTLE_INTERVAL_SECS"
  done
  echo "[elk_run_metrics] FAIL: stragglers kept arriving after the wipe for ${SETTLE_TIMEOUT_SECS}s" >&2
  return 1
}

elk_settle() {
  local deadline=$((SECONDS + SETTLE_TIMEOUT_SECS)) c prev=-1 stable=0
  echo "[elk_run_metrics] settling ingestion (timeout ${SETTLE_TIMEOUT_SECS}s, stable=${SETTLE_STABLE_POLLS} polls)"

  while (( SECONDS < deadline )); do
    c="$(elk_doc_count)"
    if (( c > 0 && c == prev )); then
      stable=$((stable + 1))
      if (( stable >= SETTLE_STABLE_POLLS )); then
        echo "[elk_run_metrics] settled: $c docs (stable for ${SETTLE_STABLE_POLLS} polls)"
        return 0
      fi
    else
      stable=0
    fi
    prev="$c"
    sleep "$SETTLE_INTERVAL_SECS"
  done

  echo "[elk_run_metrics] FAIL: ingestion did not settle — last count=$prev (0 => nothing shipped; growing => still ingesting or pipeline broken)"
  echo "[elk_run_metrics] cluster status: $(_es _cluster/health | _json_get status unknown)"
  return 1
}

_type_filter() {
  cat <<EOF
{ "bool": { "should": [
  { "term": { "type": "$1" } },
  { "term": { "type.keyword": "$1" } }
], "minimum_should_match": 1 } }
EOF
}

elk_query_metrics() {
  local body
  body="$(cat <<EOF
{
  "size": 0,
  "aggs": {
    "reads":          { "filter": $(_type_filter PhysicalCellReadLog) },
    "writes":         { "filter": $(_type_filter PhysicalCellProgramLog) },
    "logical_writes": { "filter": $(_type_filter LogicalCellProgramLog) },
    "gcs":            { "filter": $(_type_filter GarbageCollectionLog) },
    "bg_gcs":         { "filter": { "bool": { "filter": [ $(_type_filter GarbageCollectionLog), { "term": { "background": true } } ] } } },
    "util": {
      "filter": $(_type_filter SsdUtilizationLog),
      "aggs": {
        "avg":  { "avg": { "field": "utilization_percent" } },
        "max":  { "max": { "field": "utilization_percent" } }
      }
    },
    "ssd_size":    { "max": { "field": "test.ssd.size" } },
    "total_pages": { "max": { "field": "total_pages" } },
    "sim_us":      { "sum": { "field": "duration_us" } }
  }
}
EOF
)"
  local j; j="$(_es "$INDEX_PATTERN/_search" -d "$body")"

  local read_count write_count logical_count gc_count bg_gc_count
  read_count="$(printf '%s' "$j"  | _json_get aggregations.reads.doc_count 0)"
  write_count="$(printf '%s' "$j" | _json_get aggregations.writes.doc_count 0)"
  logical_count="$(printf '%s' "$j" | _json_get aggregations.logical_writes.doc_count 0)"
  gc_count="$(printf '%s' "$j"    | _json_get aggregations.gcs.doc_count 0)"
  bg_gc_count="$(printf '%s' "$j" | _json_get aggregations.bg_gcs.doc_count 0)"

  local util_avg util_max total_pages ssd_size page_size
  util_avg="$(printf '%s' "$j" | _json_get aggregations.util.avg.value 0)"
  util_max="$(printf '%s' "$j" | _json_get aggregations.util.max.value 0)"
  total_pages="$(printf '%s' "$j" | _json_get aggregations.total_pages.value)"
  ssd_size="$(printf '%s' "$j" | _json_get aggregations.ssd_size.value)"
  page_size=""
  if [[ -n "$ssd_size" && -n "$total_pages" ]] && awk -v t="$total_pages" 'BEGIN{exit !(t+0>0)}'; then
    page_size="$(awk -v s="$ssd_size" -v t="$total_pages" 'BEGIN{ printf "%.0f", s/t }')"
  fi

  local sim_us sim_secs
  sim_us="$(printf '%s' "$j" | _json_get aggregations.sim_us.value 0)"
  sim_secs="$(awk -v u="$sim_us" 'BEGIN{ printf "%.6f", u/1000000.0 }')"

  local read_iops write_iops wa read_mbps write_mbps
  read_iops="$(awk  -v c="$read_count"  -v s="$sim_secs" 'BEGIN{ if (s<=0) print 0; else printf "%.4f", c/s }')"
  write_iops="$(awk -v c="$write_count" -v s="$sim_secs" 'BEGIN{ if (s<=0) print 0; else printf "%.4f", c/s }')"
  wa="$(awk -v p="$write_count" -v l="$logical_count" 'BEGIN{ if (l<=0) print 0; else printf "%.4f", p/l }')"
  read_mbps="$(awk  -v c="$read_count"  -v p="${page_size:-0}" -v s="$sim_secs" 'BEGIN{ if (s<=0) print 0; else printf "%.4f", (c*p*8.0)/(s*1000000.0) }')"
  write_mbps="$(awk -v c="$write_count" -v p="${page_size:-0}" -v s="$sim_secs" 'BEGIN{ if (s<=0) print 0; else printf "%.4f", (c*p*8.0)/(s*1000000.0) }')"

  cat <<EOF
write_count=$write_count
logical_write_count=$logical_count
read_count=$read_count
read_iops=$read_iops
write_iops=$write_iops
write_amplification=$wa
read_speed_mbps=$read_mbps
write_speed_mbps=$write_mbps
gc_invocations=$gc_count
gc_background=$bg_gc_count
disk_utilization_avg=$util_avg
disk_utilization_max=$util_max
sim_span_secs=$sim_secs
page_size_bytes=${page_size:-0}
EOF
}

GATED_METRICS=(logical_write_count write_count write_amplification gc_invocations disk_utilization_avg)

_lt() { awk -v a="$1" -v b="$2" 'BEGIN{ exit !(a+0 < b+0) }'; }
_gt() { awk -v a="$1" -v b="$2" 'BEGIN{ exit !(a+0 > b+0) }'; }

elk_assert_metric() {
  local g="$1" m="$2" v="$3"
  local lo="BOUND_${g^^}_${m^^}_MIN" hi="BOUND_${g^^}_${m^^}_MAX"
  lo="${!lo:-}"; hi="${!hi:-}"
  if [[ -z "$lo" && -z "$hi" ]]; then
    printf '    %-24s = %-14s (no bound — report only)\n' "$m" "$v"; return 0
  fi
  local ok=1
  [[ -n "$lo" ]] && _lt "$v" "$lo" && ok=0
  [[ -n "$hi" ]] && _gt "$v" "$hi" && ok=0
  if (( ok )); then
    printf '    OK   %-24s = %-14s [%s, %s]\n' "$m" "$v" "${lo:- }" "${hi:- }"
    return 0
  fi
  printf '    FAIL %-24s = %-14s [%s, %s]\n' "$m" "$v" "${lo:- }" "${hi:- }"
  return 1
}

elk_assert_case() {
  local name="$1" metrics="$2" m val rc=0
  for m in "${GATED_METRICS[@]}"; do
    val="$(echo "$metrics" | sed -nE "s/^$m=(.*)/\1/p")"
    elk_assert_metric "$name" "$m" "$val" || rc=1
  done
  return $rc
}

# --- event-time validity ----------------------------------------------------
# Time-derived dashboard panels bucket on @timestamp. That is only meaningful
# when filebeat parsed each event's own logging_time into it; when it cannot
# (layout mismatch, or an event type shipped without the field at all)
# @timestamp silently falls back to ingest time and the panels describe when the
# log reached Elasticsearch instead of when the run happened.
EVENT_TIME_SAMPLE="${EVENT_TIME_SAMPLE:-500}"
EVENT_TIME_TOLERANCE_MS="${EVENT_TIME_TOLERANCE_MS:-1}"

_event_time_sample() {
  local query="${1:-}"
  [[ -n "$query" ]] || query='{ "match_all": {} }'
  _es "$INDEX_PATTERN/_search" -d "{
    \"size\": $EVENT_TIME_SAMPLE,
    \"_source\": [\"@timestamp\", \"logging_time\", \"type\"],
    \"query\": $query
  }"
}

# Reads one search response on stdin, prints "checked=N mismatched=N stale=N max_skew_ms=N".
# logging_time is written in the simulator container's clock and filebeat is
# configured with the matching timezone, so the two must agree to the
# millisecond Elasticsearch stores @timestamp with.
_event_time_verdict() {
  python3 -c '
import sys, json, datetime

tol_ms = float(sys.argv[1])
try:
    hits = json.load(sys.stdin)["hits"]["hits"]
except Exception:
    hits = []

checked = mismatched = stale = 0
max_skew = 0.0
for hit in hits:
    src = hit.get("_source", {})
    event, ingest = src.get("logging_time"), src.get("@timestamp")
    if not event or not ingest:
        mismatched += 1
        continue
    try:
        event_dt = datetime.datetime.strptime(
            event, "%Y-%m-%d_%H-%M-%S.%f").replace(tzinfo=datetime.timezone.utc)
        ingest_dt = datetime.datetime.fromisoformat(ingest.replace("Z", "+00:00"))
    except ValueError:
        mismatched += 1
        continue
    checked += 1
    if event_dt.year < 2000:
        stale += 1
    # Elasticsearch keeps @timestamp to the millisecond, logging_time to the
    # microsecond: compare on the resolution both of them have
    event_dt -= datetime.timedelta(microseconds=event_dt.microsecond % 1000)
    skew = abs((ingest_dt - event_dt).total_seconds()) * 1000.0
    max_skew = max(max_skew, skew)
    if skew > tol_ms:
        mismatched += 1
print("checked=%d mismatched=%d stale=%d max_skew_ms=%.3f"
      % (checked, mismatched, stale, max_skew))
' "$EVENT_TIME_TOLERANCE_MS"
}

_report() {
  local ok="$1" metric="$2" value="$3" note="${4:-}"
  if [[ "$ok" == "1" ]]; then
    printf '    OK   %-24s = %-14s %s\n' "$metric" "$value" "$note"
    return 0
  fi
  printf '    FAIL %-24s = %-14s %s\n' "$metric" "$value" "$note"
  return 1
}

elk_assert_event_time() {
  local rc=0 missing verdict checked mismatched stale skew gc_total gc_buckets body j

  missing="$(_es "$INDEX_PATTERN/_count" \
    -d '{ "query": { "bool": { "must_not": { "exists": { "field": "logging_time" } } } } }' \
    | _json_get count -1)"
  _report "$([[ "$missing" == "0" ]] && echo 1 || echo 0)" events_without_event_time "$missing" \
    "(every shipped event type must carry its own time)" || rc=1

  # a general sample plus a garbage-collection-only one: GC is a thin slice of
  # the stream and it is the type this gate was written for
  verdict="$( { _event_time_sample | _event_time_verdict
                _event_time_sample "$(_type_filter GarbageCollectionLog)" | _event_time_verdict
              } | awk -F'[= ]' '{ c += $2; m += $4; s += $6; if ($8 + 0 > k) k = $8 + 0 }
                                END { print c + 0, m + 0, s + 0, k + 0 }')"
  read -r checked mismatched stale skew <<< "$verdict"

  _report "$([[ "$checked" -gt 0 ]] && echo 1 || echo 0)" event_time_sampled "$checked" \
    "(a sample of 0 means nothing was shipped)" || rc=1
  _report "$([[ "$mismatched" == "0" ]] && echo 1 || echo 0)" event_time_mismatch "$mismatched" \
    "(@timestamp != logging_time; worst skew ${skew}ms, tolerance ${EVENT_TIME_TOLERANCE_MS}ms)" || rc=1
  _report "$([[ "$stale" == "0" ]] && echo 1 || echo 0)" event_time_unset "$stale" \
    "(events timestamped before the year 2000)" || rc=1

  # the "Garbage Collection Events Over Time" panel, as a query
  body="$(cat <<EOF
{ "size": 0, "track_total_hits": true, "query": $(_type_filter GarbageCollectionLog),
  "aggs": { "over_time": { "date_histogram":
    { "field": "@timestamp", "fixed_interval": "1s", "min_doc_count": 1 } } } }
EOF
)"
  j="$(_es "$INDEX_PATTERN/_search" -d "$body")"
  gc_total="$(printf '%s' "$j" | _json_get hits.total.value 0)"
  gc_buckets="$(printf '%s' "$j" | python3 -c '
import sys, json
try:
    print(len(json.load(sys.stdin)["aggregations"]["over_time"]["buckets"]))
except Exception:
    print(0)')"
  if [[ "$gc_total" == "0" ]]; then
    _report 1 gc_time_buckets "$gc_buckets" "(this case triggers no GC)"
  else
    _report "$([[ "$gc_buckets" -gt 0 ]] && echo 1 || echo 0)" gc_time_buckets "$gc_buckets" \
      "($gc_total GC events, bucketed over event time)" || rc=1
  fi

  return $rc
}
