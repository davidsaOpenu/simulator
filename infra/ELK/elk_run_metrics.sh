#!/bin/bash
# =============================================================================
# elk_run_metrics.sh  —  run.id-scoped ELK metric extraction + settle
#
# Sourceable helper library used by docker-test-host-elk.sh. It reuses the
# curl-to-Elasticsearch metric-retrieval approach from elk_performance_test.sh,
# but scopes every query to a single RUN_ID (the per-invocation id stamped on
# every event) instead of a fragile time window. This gives order-independent
# isolation, and metrics are validated against per-case bounds (not exact event
# accounting), so cases that ship a stable subset are still gateable.
#
# Public functions:
#   elk_settle        RUN_ID   -> 0 once the run.id doc count stops growing, else 1
#   elk_query_metrics RUN_ID   -> prints "KEY=VALUE" lines for the metrics
#
# Env knobs: ES_URL, INDEX_PATTERN, ELASTIC_PASSWORD, SETTLE_TIMEOUT_SECS,
#            SETTLE_INTERVAL_SECS, SETTLE_STABLE_POLLS.
# =============================================================================

ES_URL="${ES_URL:-https://localhost:9200}"
INDEX_PATTERN="${INDEX_PATTERN:-filebeat-*}"
# Generous default timeout so heavy cases (millions of events) can finish shipping.
SETTLE_TIMEOUT_SECS="${SETTLE_TIMEOUT_SECS:-600}"
SETTLE_INTERVAL_SECS="${SETTLE_INTERVAL_SECS:-5}"
# Consecutive unchanged polls (count>0) that count as "ingestion settled".
SETTLE_STABLE_POLLS="${SETTLE_STABLE_POLLS:-3}"

_es() {  # _es <path> [curl-data-args...]
  local path="$1"; shift
  curl -k -s -u "elastic:${ELASTIC_PASSWORD:-changeme}" \
    -H 'Content-Type: application/json' "$ES_URL/$path" "$@"
}

# Count of documents in ES carrying this run.id.
elk_run_count() {
  local run_id="$1" body
  body="$(cat <<EOF
{ "query": { "term": { "run.id": "$run_id" } } }
EOF
)"
  _es "$INDEX_PATTERN/_count" -d "$body" | jq -r '.count // 0'
}

# -----------------------------------------------------------------------------
# Settle: wait until the run.id doc count in ES stops growing (ingestion has
# caught up), then let the caller assert metrics against bounds. This replaces
# the old exact "ES count == emitted manifest" reconciliation, which required a
# perfectly lossless, fully-shipped pipeline and so excluded the heavy / partial
# cases. We only need the numbers to stop moving before we read them; the bounds
# (with tolerance) are what catch a genuinely wrong result.
#
# Returns 0 once the count is >0 and unchanged for SETTLE_STABLE_POLLS polls.
# Times out to a hard failure only if nothing ever ships (count stays 0) or the
# count never stabilises within SETTLE_TIMEOUT_SECS (still shipping / broken).
# -----------------------------------------------------------------------------
elk_settle() {
  local run_id="$1"
  local deadline=$((SECONDS + SETTLE_TIMEOUT_SECS)) c prev=-1 stable=0
  echo "[elk_run_metrics] settling run.id=$run_id (timeout ${SETTLE_TIMEOUT_SECS}s, stable=${SETTLE_STABLE_POLLS} polls)"

  while (( SECONDS < deadline )); do
    c="$(elk_run_count "$run_id")"
    if (( c > 0 && c == prev )); then
      stable=$((stable + 1))
      if (( stable >= SETTLE_STABLE_POLLS )); then
        echo "[elk_run_metrics] settled: run.id=$run_id holds $c docs (stable for ${SETTLE_STABLE_POLLS} polls)"
        return 0
      fi
    else
      stable=0
    fi
    prev="$c"
    sleep "$SETTLE_INTERVAL_SECS"
  done

  # Timed out: never assert on a count that is still moving (or zero).
  echo "[elk_run_metrics] FAIL: run.id=$run_id did not settle — last count=$prev (0 => nothing shipped; growing => still ingesting or pipeline broken)"
  echo "[elk_run_metrics] cluster health: $(_es _cluster/health | jq -c '{status,number_of_nodes,active_shards}' 2>/dev/null)"
  return 1
}

# -----------------------------------------------------------------------------
# Type filter that tolerates either `type` or `type.keyword` mapping.
# -----------------------------------------------------------------------------
_type_filter() {
  cat <<EOF
{ "bool": { "should": [
  { "term": { "type": "$1" } },
  { "term": { "type.keyword": "$1" } }
], "minimum_should_match": 1 } }
EOF
}

# -----------------------------------------------------------------------------
# elk_query_metrics RUN_ID -> the metrics as KEY=VALUE lines.
#
# Counts / write-amplification / GC / utilization come straight from run.id
# aggregations. IOPS and speed use SUM(duration_us) — the total modeled
# device-busy time — as the time base: deterministic and machine-independent
# (wall clock plays no part). The simulated timestamps (logging_time) are
# keyword strings in ES and can't be aggregated numerically; duration_us (the
# per-event modeled latency, a numeric field) is the right signal.
# -----------------------------------------------------------------------------
elk_query_metrics() {
  local run_id="$1"
  local body
  body="$(cat <<EOF
{
  "size": 0,
  "query": { "term": { "run.id": "$run_id" } },
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
        "max":  { "max": { "field": "utilization_percent" } },
        "last": { "top_hits": { "size": 1, "sort": [ { "logging_time": { "order": "desc" } } ], "_source": { "includes": ["utilization_percent","total_pages","occupied_pages","test.ssd.size"] } } }
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
  read_count="$(echo "$j"  | jq -r '.aggregations.reads.doc_count // 0')"
  write_count="$(echo "$j" | jq -r '.aggregations.writes.doc_count // 0')"
  logical_count="$(echo "$j" | jq -r '.aggregations.logical_writes.doc_count // 0')"
  gc_count="$(echo "$j"    | jq -r '.aggregations.gcs.doc_count // 0')"
  bg_gc_count="$(echo "$j" | jq -r '.aggregations.bg_gcs.doc_count // 0')"

  local util_avg util_max occupied total_pages ssd_size page_size
  util_avg="$(echo "$j" | jq -r '.aggregations.util.avg.value // 0')"
  util_max="$(echo "$j" | jq -r '.aggregations.util.max.value // 0')"
  occupied="$(echo "$j" | jq -r '.aggregations.util.last.hits.hits[0]._source.occupied_pages // empty')"
  # ssd_size and total_pages are per-run constants; max() is deterministic (unlike a
  # "last doc by logging_time" top_hits, whose last doc may lack these fields -> page_size 0).
  total_pages="$(echo "$j" | jq -r '.aggregations.total_pages.value // empty')"
  ssd_size="$(echo "$j" | jq -r '.aggregations.ssd_size.value // empty')"
  page_size=""
  if [[ -n "$ssd_size" && -n "$total_pages" ]] && awk -v t="$total_pages" 'BEGIN{exit !(t+0>0)}'; then
    page_size="$(awk -v s="$ssd_size" -v t="$total_pages" 'BEGIN{ printf "%.0f", s/t }')"
  fi

  # Simulated time base = total modeled device-busy time = SUM(duration_us).
  local sim_us sim_secs
  sim_us="$(echo "$j" | jq -r '.aggregations.sim_us.value // 0')"
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
