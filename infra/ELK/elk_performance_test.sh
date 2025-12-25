#!/bin/bash
# ELK performance test: P95 latency + IOPS + write amplification checks over a recent time window.

set -euo pipefail
trap 'ec=$?; echo "[elk_performance_test] FAILED (exit $ec) on: $BASH_COMMAND" >&2' ERR

ELK_DIR="${1:-$(pwd)}"
ES_URL="${ES_URL:-https://localhost:9200}"
INDEX_PATTERN="${INDEX_PATTERN:-filebeat-*}"
SAMPLES="${SAMPLES:-30}"
P95_LIMIT_MS="${P95_LIMIT_MS:-500}"

MIN_IOPS_READ="${MIN_IOPS_READ:-2.0}"
MIN_IOPS_WRITE="${MIN_IOPS_WRITE:-2.0}"
MAX_WA="${MAX_WA:-1.5}"

WAIT_SECS="${WAIT_SECS:-60}"
LOOKBACK_HOURS="${LOOKBACK_HOURS:-3}"
FILEBEAT_WAIT_SECS="${FILEBEAT_WAIT_SECS:-120}"

FROM_DATE="$(date -u -d "${LOOKBACK_HOURS} hours ago" +%Y-%m-%dT%H:%M:%SZ)"
TO_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

cd "$ELK_DIR"

if [[ -f .env && -z "${ELASTIC_PASSWORD:-}" ]]; then
  set -o allexport
  source .env
  set +o allexport
fi
: "${ELASTIC_PASSWORD:?ELASTIC_PASSWORD must be set or present in .env}"

unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY

echo "[elk_performance_test] ES_URL=$ES_URL INDEX_PATTERN=$INDEX_PATTERN SAMPLES=$SAMPLES P95_LIMIT_MS=$P95_LIMIT_MS"
echo "[elk_performance_test] thresholds: MIN_IOPS_READ=$MIN_IOPS_READ MIN_IOPS_WRITE=$MIN_IOPS_WRITE MAX_WA=$MAX_WA"
echo "[elk_performance_test] time window: LOOKBACK_HOURS=$LOOKBACK_HOURS FILEBEAT_WAIT_SECS=$FILEBEAT_WAIT_SECS"

sec_to_ms() {
  local s="$1"
  awk -v v="$s" 'BEGIN {
    if (v == "" || v == "-") { print 0; exit }
    printf "%d\n", v * 1000
  }'
}

lt() { awk -v v="$1" -v m="$2" 'BEGIN { exit !(v+0 < m+0) }'; }
gt() { awk -v v="$1" -v m="$2" 'BEGIN { exit !(v+0 > m+0) }'; }

iso_to_logtime_from() { echo "$1" | sed -E 's/T/_/; s/:/-/g; s/Z$/.000000/'; }
iso_to_logtime_to()   { echo "$1" | sed -E 's/T/_/; s/:/-/g; s/Z$/.999999/'; }

list_indices_sorted() {
  curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
    "$ES_URL/_cat/indices/$INDEX_PATTERN?h=index" \
    | awk 'NF' \
    | sort -r
}

pick_ts_field_for_index() {
  local index="$1"
  local caps
  caps="$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
    "$ES_URL/$index/_field_caps?fields=@timestamp,logging_time" || true)"

  if command -v jq >/dev/null 2>&1; then
    if echo "$caps" | jq -e '.fields["@timestamp"] != null' >/dev/null 2>&1; then
      echo "@timestamp"
    else
      echo "logging_time"
    fi
  else
    if echo "$caps" | grep -q '"@timestamp"'; then
      echo "@timestamp"
    else
      echo "logging_time"
    fi
  fi
}

TS_FIELD="@timestamp"
TS_FROM=""
TS_TO=""

set_ts_bounds() {
  if [[ "$TS_FIELD" == "@timestamp" ]]; then
    TS_FROM="$FROM_DATE"
    TS_TO="$TO_DATE"
  else
    TS_FROM="$(iso_to_logtime_from "$FROM_DATE")"
    TS_TO="$(iso_to_logtime_to "$TO_DATE")"
  fi
}

count_body() {
  cat <<EOF
{
  "query": {
    "range": {
      "$TS_FIELD": {
        "gte": "$TS_FROM",
        "lte": "$TS_TO"
      }
    }
  }
}
EOF
}

search_body() {
  local size="${1:-1}"
  cat <<EOF
{
  "size": $size,
  "track_total_hits": false,
  "query": {
    "range": {
      "$TS_FIELD": {
        "gte": "$TS_FROM",
        "lte": "$TS_TO"
      }
    }
  },
  "sort": [
    { "$TS_FIELD": { "order": "desc" } }
  ]
}
EOF
}

count_in_window() {
  local index="$1"
  local count_json count

  TS_FIELD="$(pick_ts_field_for_index "$index")"
  set_ts_bounds

  count_json="$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
    -H 'Content-Type: application/json' \
    "$ES_URL/$index/_count" -d "$(count_body)")"

  if command -v jq >/dev/null 2>&1; then
    count="$(echo "$count_json" | jq -r '.count // 0')"
  else
    count="$(echo "$count_json" | grep -o '"count":[0-9]*' | grep -o '[0-9]*' || echo "0")"
  fi

  echo "$count"
}

pick_active_index() {
  local idx_try c first
  first=""

  while IFS= read -r idx_try; do
    [[ -z "$idx_try" ]] && continue
    [[ -z "$first" ]] && first="$idx_try"

    c="$(count_in_window "$idx_try")"
    if [[ "$c" -gt 0 ]]; then
      echo "$idx_try"
      return 0
    fi
  done <<< "$(list_indices_sorted)"

  if [[ -n "$first" ]]; then
    echo "$first"
    return 0
  fi

  curl -k -s -u "elastic:$ELASTIC_PASSWORD" "$ES_URL/_cat/indices?h=index" | awk 'NF{print; exit}'
}

deadline=$((SECONDS + WAIT_SECS))
while (( SECONDS < deadline )); do
  if curl -k -s -u "elastic:$ELASTIC_PASSWORD" "$ES_URL/_cluster/health" >/dev/null 2>&1; then
    echo "[elk_performance_test] Elasticsearch is up"
    break
  fi
  echo "[elk_performance_test] waiting for ES..."
  sleep 3
done

idx="$(pick_active_index)"
[[ -n "$idx" ]] || { echo "[elk_performance_test] no indices found"; exit 1; }

TS_FIELD="$(pick_ts_field_for_index "$idx")"
set_ts_bounds

echo "[elk_performance_test] using disk index: $idx"
echo "[elk_performance_test] using TS_FIELD=$TS_FIELD"

echo "[elk_performance_test] waiting for Filebeat to index logs (up to ${FILEBEAT_WAIT_SECS}s)..."
deadline=$((SECONDS + FILEBEAT_WAIT_SECS))
start_time=$SECONDS
data_found=0

while (( SECONDS < deadline )); do
  TO_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

  elapsed=$((SECONDS - start_time))
  if (( elapsed >= 20 && elapsed % 20 == 0 )); then
    new_idx="$(pick_active_index)"
    if [[ -n "$new_idx" && "$new_idx" != "$idx" ]]; then
      idx="$new_idx"
      TS_FIELD="$(pick_ts_field_for_index "$idx")"
      echo "[elk_performance_test] switched active index to: $idx"
      echo "[elk_performance_test] using TS_FIELD=$TS_FIELD"
    fi
  fi

  set_ts_bounds

  count_json="$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
    -H 'Content-Type: application/json' \
    "$ES_URL/$idx/_count" -d "$(count_body)")"

  if command -v jq >/dev/null 2>&1; then
    count="$(echo "$count_json" | jq -r '.count // 0')"
  else
    count="$(echo "$count_json" | grep -o '"count":[0-9]*' | grep -o '[0-9]*' || echo "0")"
  fi

  if [[ "$count" -gt 0 ]]; then
    elapsed=$((SECONDS - start_time))
    echo "[elk_performance_test] found $count documents in time window after ${elapsed}s, proceeding with test"
    data_found=1
    break
  fi

  elapsed=$((SECONDS - start_time))
  echo "[elk_performance_test] no data found yet (waited ${elapsed}s), waiting..."
  sleep 5
done

set_ts_bounds

if [[ "$data_found" -eq 0 ]]; then
  echo "[elk_performance_test] WARNING: no data found in index $idx within time window after ${FILEBEAT_WAIT_SECS}s"
  echo "[elk_performance_test] time window: FROM=$FROM_DATE TO=$TO_DATE (TS_FIELD=$TS_FIELD)"

  total_count_json="$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" "$ES_URL/$idx/_count")"
  if command -v jq >/dev/null 2>&1; then
    total_count="$(echo "$total_count_json" | jq -r '.count // 0')"
  else
    total_count="$(echo "$total_count_json" | grep -o '"count":[0-9]*' | grep -o '[0-9]*' || echo "0")"
  fi
  echo "[elk_performance_test] total documents in index: $total_count"

  if [[ "$total_count" -gt 0 ]]; then
    echo "[elk_performance_test] trying wider time window (last 24 hours)..."
    FROM_DATE="$(date -u -d '24 hours ago' +%Y-%m-%dT%H:%M:%SZ)"
    TO_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    set_ts_bounds
    echo "[elk_performance_test] new time window: FROM=$FROM_DATE TO=$TO_DATE"
  else
    echo "[elk_performance_test] ERROR: index is empty, Filebeat may not be shipping logs"
    exit 1
  fi
fi

echo "-----------------------------------------------------------------------"
echo "[elk_performance_test] fetching sample record from $idx..."
sample_json="$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
  -H 'Content-Type: application/json' \
  "$ES_URL/$idx/_search" -d "$(search_body 1)")"

if command -v jq >/dev/null 2>&1; then
  echo "$sample_json" | jq '.hits.hits[0]._source'
else
  echo "$sample_json"
fi
echo "[elk_performance_test] end of sample record"
echo "-----------------------------------------------------------------------"

lat_file="$(mktemp)"
trap 'rm -f "$lat_file"' EXIT

i=1
while [ "$i" -le "$SAMPLES" ]; do
  t_sec="$(curl -k -s -o /dev/null -w '%{time_total}' \
    -u "elastic:$ELASTIC_PASSWORD" \
    -H 'Content-Type: application/json' \
    "$ES_URL/$idx/_search" -d "$(search_body 0)")"
  ms="$(sec_to_ms "$t_sec")"
  echo "$ms" >>"$lat_file"
  i=$((i + 1))
done

n="$(wc -l <"$lat_file")"
[[ "$n" -gt 0 ]] || { echo "[elk_performance_test] no samples collected"; exit 1; }

p95_idx=$(((95 * n + 99) / 100))
if (( p95_idx < 1 )); then p95_idx=1; fi
if (( p95_idx > n )); then p95_idx="$n"; fi

p95="$(sort -n "$lat_file" | sed -n "${p95_idx}p")"
echo "[elk_performance_test] samples=$n P95=${p95}ms limit=${P95_LIMIT_MS}ms"

if (( p95 > P95_LIMIT_MS )); then
  echo "[elk_performance_test] PERFORMANCE FAIL (P95 ${p95}ms > ${P95_LIMIT_MS}ms)"
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "[elk_performance_test] jq is required for metrics checks"
  exit 1
fi

TO_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
set_ts_bounds

FROM_EPOCH="$(date -u -d "$FROM_DATE" +%s)"
TO_EPOCH="$(date -u -d "$TO_DATE" +%s)"
INTERVAL_SECS=$((TO_EPOCH - FROM_EPOCH))
if (( INTERVAL_SECS <= 0 )); then
  echo "[elk_performance_test] invalid time window: INTERVAL_SECS=$INTERVAL_SECS"
  exit 1
fi

type_filter() {
  local t="$1"
  cat <<EOF
{
  "bool": {
    "should": [
      { "term": { "type": "$t" } },
      { "term": { "type.keyword": "$t" } }
    ],
    "minimum_should_match": 1
  }
}
EOF
}

metrics_json="$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
  -H 'Content-Type: application/json' \
  "$ES_URL/$idx/_search?size=0" -d @- <<EOF
{
  "query": {
    "range": {
      "$TS_FIELD": { "gte": "$TS_FROM", "lte": "$TS_TO" }
    }
  },
  "aggs": {
    "read_count": {
      "filter": $(type_filter "PhysicalCellReadLog")
    },
    "background_read_count": {
      "filter": {
        "bool": {
          "filter": [
            $(type_filter "PhysicalCellReadLog"),
            { "term": { "background": true } }
          ]
        }
      }
    },

    "write_count": {
      "filter": $(type_filter "PhysicalCellProgramLog")
    },
    "background_write_count": {
      "filter": {
        "bool": {
          "filter": [
            $(type_filter "PhysicalCellProgramLog"),
            { "term": { "background": true } }
          ]
        }
      }
    },

    "logical_write_count": {
      "filter": $(type_filter "LogicalCellProgramLog")
    },

    "garbage_collection_count": {
      "filter": $(type_filter "GarbageCollectionLog")
    },
    "background_garbage_collection_count": {
      "filter": {
        "bool": {
          "filter": [
            $(type_filter "GarbageCollectionLog"),
            { "term": { "background": true } }
          ]
        }
      }
    },

    "utilization_last": {
      "filter": $(type_filter "SsdUtilizationLog"),
      "aggs": {
        "last": {
          "top_hits": {
            "size": 1,
            "sort": [ { "$TS_FIELD": { "order": "desc" } } ],
            "_source": {
              "includes": ["utilization_percent","total_pages","occupied_pages","test.ssd.size"]
            }
          }
        }
      }
    }
  }
}
EOF
)"

read_count="$(echo "$metrics_json" | jq -r '.aggregations.read_count.doc_count // 0')"
background_read_count="$(echo "$metrics_json" | jq -r '.aggregations.background_read_count.doc_count // 0')"

write_count="$(echo "$metrics_json" | jq -r '.aggregations.write_count.doc_count // 0')"
background_write_count="$(echo "$metrics_json" | jq -r '.aggregations.background_write_count.doc_count // 0')"

logical_write_count="$(echo "$metrics_json" | jq -r '.aggregations.logical_write_count.doc_count // 0')"

garbage_collection_count="$(echo "$metrics_json" | jq -r '.aggregations.garbage_collection_count.doc_count // 0')"
background_garbage_collection_count="$(echo "$metrics_json" | jq -r '.aggregations.background_garbage_collection_count.doc_count // 0')"

read_iops="$(awk -v c="$read_count" -v d="$INTERVAL_SECS" 'BEGIN { if (d<=0) print 0; else printf "%.6f", c/d }')"
write_iops="$(awk -v c="$write_count" -v d="$INTERVAL_SECS" 'BEGIN { if (d<=0) print 0; else printf "%.6f", c/d }')"

write_amplification="$(awk -v p="$write_count" -v l="$logical_write_count" 'BEGIN { if (l<=0) print 0; else printf "%.6f", p/l }')"

util_src="$(echo "$metrics_json" | jq -c '.aggregations.utilization_last.last.hits.hits[0]._source // {}')"
utilization="$(echo "$util_src" | jq -r '.utilization_percent // empty')"
total_pages="$(echo "$util_src" | jq -r '.total_pages // empty')"
occupied_pages="$(echo "$util_src" | jq -r '.occupied_pages // empty')"
ssd_size_bytes="$(echo "$util_src" | jq -r '."test.ssd.size" // empty')"

if [[ -z "${utilization:-}" && -n "${total_pages:-}" && -n "${occupied_pages:-}" ]]; then
  utilization="$(awk -v o="$occupied_pages" -v t="$total_pages" 'BEGIN{ if (t<=0) print ""; else printf "%.12f", o/t }')"
fi

page_size_bytes=""
if [[ -n "${ssd_size_bytes:-}" && -n "${total_pages:-}" ]]; then
  page_size_bytes="$(awk -v s="$ssd_size_bytes" -v t="$total_pages" 'BEGIN{ if (t<=0) print ""; else printf "%.12f", s/t }')"
fi

read_speed_mbps="-"
write_speed_mbps="-"
if [[ -n "${page_size_bytes:-}" ]]; then
  read_speed_mbps="$(awk -v c="$read_count" -v p="$page_size_bytes" -v d="$INTERVAL_SECS" \
    'BEGIN{ if (d<=0) print 0; else printf "%.6f", (c*p*8.0)/(d*1000000.0) }')"
  write_speed_mbps="$(awk -v c="$write_count" -v p="$page_size_bytes" -v d="$INTERVAL_SECS" \
    'BEGIN{ if (d<=0) print 0; else printf "%.6f", (c*p*8.0)/(d*1000000.0) }')"
fi

util_percent="UNKNOWN"
if [[ -n "${utilization:-}" ]]; then
  util_percent="$(awk -v u="$utilization" 'BEGIN{ printf "%.4f", u*100.0 }')"
fi

echo "[elk_performance_test] monitor window: FROM=$FROM_DATE TO=$TO_DATE (secs=$INTERVAL_SECS) TS_FIELD=$TS_FIELD index=$idx"
echo "[elk_performance_test] write_count=$write_count read_count=$read_count logical_write_count=$logical_write_count"
echo "[elk_performance_test] background_write_count=$background_write_count background_read_count=$background_read_count"
echo "[elk_performance_test] garbage_collection_count=$garbage_collection_count background_garbage_collection_count=$background_garbage_collection_count"
echo "[elk_performance_test] write_amplification=$write_amplification"
echo "[elk_performance_test] write_speed(Mb/s)=$write_speed_mbps read_speed(Mb/s)=$read_speed_mbps"
echo "[elk_performance_test] utilization=${utilization:-UNKNOWN} (${util_percent}%) page_size_bytes=${page_size_bytes:-UNKNOWN}"
echo "[elk_performance_test] avg_read_iops=$read_iops avg_write_iops=$write_iops"

test_fail=0

if lt "$read_iops" "$MIN_IOPS_READ"; then
  echo "[elk_performance_test] FAIL: read IOPS ($read_iops) < MIN_IOPS_READ ($MIN_IOPS_READ)"
  test_fail=1
else
  echo "[elk_performance_test] OK: read IOPS ($read_iops) >= MIN_IOPS_READ ($MIN_IOPS_READ)"
fi

if lt "$write_iops" "$MIN_IOPS_WRITE"; then
  echo "[elk_performance_test] FAIL: write IOPS ($write_iops) < MIN_IOPS_WRITE ($MIN_IOPS_WRITE)"
  test_fail=1
else
  echo "[elk_performance_test] OK: write IOPS ($write_iops) >= MIN_IOPS_WRITE ($MIN_IOPS_WRITE)"
fi

if gt "$write_amplification" "$MAX_WA"; then
  echo "[elk_performance_test] FAIL: write amplification ($write_amplification) > MAX_WA ($MAX_WA)"
  test_fail=1
else
  echo "[elk_performance_test] OK: write amplification ($write_amplification) <= MAX_WA ($MAX_WA)"
fi

if (( test_fail == 1 )); then
  echo "[elk_performance_test] METRICS TEST FAILED"
  exit 1
fi

echo "[elk_performance_test] PERFORMANCE OK"
