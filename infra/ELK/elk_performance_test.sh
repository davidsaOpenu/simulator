#!/bin/bash
# ELK performance test: P95 latency + IOPS + write amplification checks over a recent time window.

set -euo pipefail
trap 'ec=$?; echo "[elk_performance_test] FAILED (exit $ec) on: $BASH_COMMAND" >&2' ERR

ELK_DIR="${1:-$(pwd)}"
ES_URL="${ES_URL:-https://localhost:9200}"
INDEX_PATTERN="${INDEX_PATTERN:-filebeat-*}"
SAMPLES="${SAMPLES:-30}"
P95_LIMIT_MS="${P95_LIMIT_MS:-500}"

MIN_IOPS_READ="${MIN_IOPS_READ:-25.0}"
MIN_IOPS_WRITE="${MIN_IOPS_WRITE:-25.0}"

MIN_WA="${MIN_WA:-0.95}"
MAX_WA="${MAX_WA:-1.5}"

MIN_READ_SPEED_MBPS="${MIN_READ_SPEED_MBPS:-1.0}"
MIN_WRITE_SPEED_MBPS="${MIN_WRITE_SPEED_MBPS:-1.0}"

MAX_GC_RATE="${MAX_GC_RATE:-30.0}"

MIN_READ_COUNT="${MIN_READ_COUNT:-1000}"
MIN_WRITE_COUNT="${MIN_WRITE_COUNT:-1000}"
MIN_LOGICAL_WRITE_COUNT="${MIN_LOGICAL_WRITE_COUNT:-1000}"

REQUIRE_UTILIZATION="${REQUIRE_UTILIZATION:-1}"

GC_FOREGROUND_TRIGGER="${GC_FOREGROUND_TRIGGER:-1}"
MIN_UTIL_AT_FG_GC="${MIN_UTIL_AT_FG_GC:-0.90}"

SECTOR_MIN_WRITE_COUNT="${SECTOR_MIN_WRITE_COUNT:-1000}"
SECTOR_MIN_READ_COUNT="${SECTOR_MIN_READ_COUNT:-0}"
SECTOR_MIN_IOPS_WRITE="${SECTOR_MIN_IOPS_WRITE:-25.0}"
SECTOR_MIN_IOPS_READ="${SECTOR_MIN_IOPS_READ:-0.0}"
SECTOR_MIN_WRITE_SPEED_MBPS="${SECTOR_MIN_WRITE_SPEED_MBPS:-1.0}"
SECTOR_MIN_READ_SPEED_MBPS="${SECTOR_MIN_READ_SPEED_MBPS:-0.0}"
SECTOR_MIN_WA="${SECTOR_MIN_WA:-0.95}"
SECTOR_MAX_WA="${SECTOR_MAX_WA:-1.5}"

WAIT_SECS="${WAIT_SECS:-60}"
LOOKBACK_HOURS="${LOOKBACK_HOURS:-3}"
FILEBEAT_WAIT_SECS="${FILEBEAT_WAIT_SECS:-120}"

START_AT="${START_AT:-}"
PRINT_QUERY_CMD="${PRINT_QUERY_CMD:-1}"

TEST_NAME_FILTER="${TEST_NAME_FILTER:-MixSequentialAndRandomOnePageAtTimeWrite}"

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
echo "[elk_performance_test] thresholds: MIN_IOPS_READ=$MIN_IOPS_READ MIN_IOPS_WRITE=$MIN_IOPS_WRITE MIN_WA=$MIN_WA MAX_WA=$MAX_WA"
echo "[elk_performance_test] speed thresholds (Mb/s): MIN_READ_SPEED_MBPS=$MIN_READ_SPEED_MBPS MIN_WRITE_SPEED_MBPS=$MIN_WRITE_SPEED_MBPS"
echo "[elk_performance_test] minimum activity: MIN_READ_COUNT=$MIN_READ_COUNT MIN_WRITE_COUNT=$MIN_WRITE_COUNT MIN_LOGICAL_WRITE_COUNT=$MIN_LOGICAL_WRITE_COUNT"
echo "[elk_performance_test] GC: MAX_GC_RATE=$MAX_GC_RATE, FG check: GC_FOREGROUND_TRIGGER=$GC_FOREGROUND_TRIGGER MIN_UTIL_AT_FG_GC=$MIN_UTIL_AT_FG_GC"
echo "[elk_performance_test] sector test thresholds: SECTOR_MIN_WRITE_COUNT=$SECTOR_MIN_WRITE_COUNT SECTOR_MIN_IOPS_WRITE=$SECTOR_MIN_IOPS_WRITE SECTOR_MIN_WRITE_SPEED_MBPS=$SECTOR_MIN_WRITE_SPEED_MBPS SECTOR_MIN_WA=$SECTOR_MIN_WA SECTOR_MAX_WA=$SECTOR_MAX_WA"
echo "[elk_performance_test] time window: LOOKBACK_HOURS=$LOOKBACK_HOURS FILEBEAT_WAIT_SECS=$FILEBEAT_WAIT_SECS START_AT=${START_AT:-<unset>}"
echo "[elk_performance_test] test filter: TEST_NAME_FILTER=${TEST_NAME_FILTER:-<unset>}"

sec_to_ms() {
  local s="$1"
  awk -v v="$s" 'BEGIN {
    if (v == "" || v == "-") { print 0; exit }
    printf "%.0f\n", v * 1000
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
(( p95_idx < 1 )) && p95_idx=1
(( p95_idx > n )) && p95_idx="$n"

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

METRICS_FROM_DATE="$FROM_DATE"
if [[ -n "${START_AT:-}" ]]; then
  start_epoch="$(date -u -d "$START_AT" +%s 2>/dev/null || true)"
  if [[ -z "${start_epoch:-}" ]]; then
    echo "[elk_performance_test] invalid START_AT: $START_AT"
    exit 1
  fi
  if (( start_epoch >= TO_EPOCH )); then
    echo "[elk_performance_test] invalid START_AT: $START_AT (must be < TO_DATE=$TO_DATE)"
    exit 1
  fi
  if (( start_epoch > FROM_EPOCH )); then
    METRICS_FROM_DATE="$START_AT"
  fi
fi

if [[ "$TS_FIELD" == "@timestamp" ]]; then
  METRICS_TS_FROM="$METRICS_FROM_DATE"
else
  METRICS_TS_FROM="$(iso_to_logtime_from "$METRICS_FROM_DATE")"
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

test_name_filter() {
  local test_name="$1"
  cat <<EOF
{
  "bool": {
    "should": [
      { "prefix": { "test.name": "$test_name" } },
      { "prefix": { "test.name.keyword": "$test_name" } }
    ],
    "minimum_should_match": 1
  }
}
EOF
}

top_first_body() {
  cat <<EOF
{
  "size": 1,
  "sort": [ { "$TS_FIELD": { "order": "asc" } } ],
  "_source": { "includes": ["$TS_FIELD"] }
}
EOF
}

top_last_body() {
  cat <<EOF
{
  "size": 1,
  "sort": [ { "$TS_FIELD": { "order": "desc" } } ],
  "_source": { "includes": ["$TS_FIELD"] }
}
EOF
}

# =============================================================================
# GLOBAL METRICS QUERY (no test.name filter)
# =============================================================================
global_metrics_body="$(cat <<EOF
{
  "query": {
    "range": {
      "$TS_FIELD": { "gte": "$METRICS_TS_FROM", "lte": "$TS_TO" }
    }
  },
  "aggs": {
    "reads": {
      "filter": $(type_filter "PhysicalCellReadLog"),
      "aggs": {
        "first": { "top_hits": $(top_first_body) },
        "last":  { "top_hits": $(top_last_body) }
      }
    },
    "background_reads": {
      "filter": {
        "bool": {
          "filter": [
            $(type_filter "PhysicalCellReadLog"),
            { "term": { "background": true } }
          ]
        }
      }
    },

    "writes": {
      "filter": $(type_filter "PhysicalCellProgramLog"),
      "aggs": {
        "first": { "top_hits": $(top_first_body) },
        "last":  { "top_hits": $(top_last_body) }
      }
    },
    "background_writes": {
      "filter": {
        "bool": {
          "filter": [
            $(type_filter "PhysicalCellProgramLog"),
            { "term": { "background": true } }
          ]
        }
      }
    },

    "logical_writes": {
      "filter": $(type_filter "LogicalCellProgramLog"),
      "aggs": {
        "first": { "top_hits": $(top_first_body) },
        "last":  { "top_hits": $(top_last_body) }
      }
    },

    "gcs": {
      "filter": $(type_filter "GarbageCollectionLog"),
      "aggs": {
        "first": { "top_hits": $(top_first_body) },
        "last":  { "top_hits": $(top_last_body) }
      }
    },
    "background_gcs": {
      "filter": {
        "bool": {
          "filter": [
            $(type_filter "GarbageCollectionLog"),
            { "term": { "background": true } }
          ]
        }
      }
    },

    "activity": {
      "filter": {
        "bool": {
          "should": [
            $(type_filter "PhysicalCellReadLog"),
            $(type_filter "PhysicalCellProgramLog"),
            $(type_filter "LogicalCellProgramLog"),
            $(type_filter "GarbageCollectionLog")
          ],
          "minimum_should_match": 1
        }
      },
      "aggs": {
        "first": { "top_hits": $(top_first_body) },
        "last":  { "top_hits": $(top_last_body) }
      }
    },

    "utilization": {
      "filter": $(type_filter "SsdUtilizationLog"),
      "aggs": {
        "count": { "value_count": { "field": "$TS_FIELD" } },
        "avg_util": { "avg": { "field": "utilization_percent" } },
        "last": {
          "top_hits": {
            "size": 1,
            "sort": [ { "$TS_FIELD": { "order": "desc" } } ],
            "_source": { "includes": ["utilization_percent","total_pages","occupied_pages","test.ssd.size"] }
          }
        },
        "max_occ": {
          "top_hits": {
            "size": 1,
            "sort": [
              { "occupied_pages": { "order": "desc", "unmapped_type": "long" } },
              { "$TS_FIELD": { "order": "desc" } }
            ],
            "_source": { "includes": ["utilization_percent","total_pages","occupied_pages","test.ssd.size"] }
          }
        },
        "min_occ": {
          "top_hits": {
            "size": 1,
            "sort": [
              { "occupied_pages": { "order": "asc", "unmapped_type": "long" } },
              { "$TS_FIELD": { "order": "desc" } }
            ],
            "_source": { "includes": ["utilization_percent","total_pages","occupied_pages","test.ssd.size"] }
          }
        }
      }
    }
  }
}
EOF
)"

# =============================================================================
# SECTOR TEST METRICS QUERY (with test.name filter)
# =============================================================================
sector_metrics_body="$(cat <<EOF
{
  "query": {
    "bool": {
      "must": [
        {
          "range": {
            "$TS_FIELD": { "gte": "$METRICS_TS_FROM", "lte": "$TS_TO" }
          }
        },
        $(test_name_filter "$TEST_NAME_FILTER")
      ]
    }
  },
  "aggs": {
    "reads": {
      "filter": $(type_filter "PhysicalCellReadLog"),
      "aggs": {
        "first": { "top_hits": $(top_first_body) },
        "last":  { "top_hits": $(top_last_body) }
      }
    },
    "writes": {
      "filter": $(type_filter "PhysicalCellProgramLog"),
      "aggs": {
        "first": { "top_hits": $(top_first_body) },
        "last":  { "top_hits": $(top_last_body) }
      }
    },
    "logical_writes": {
      "filter": $(type_filter "LogicalCellProgramLog"),
      "aggs": {
        "first": { "top_hits": $(top_first_body) },
        "last":  { "top_hits": $(top_last_body) }
      }
    },
    "activity": {
      "filter": {
        "bool": {
          "should": [
            $(type_filter "PhysicalCellReadLog"),
            $(type_filter "PhysicalCellProgramLog"),
            $(type_filter "LogicalCellProgramLog")
          ],
          "minimum_should_match": 1
        }
      },
      "aggs": {
        "first": { "top_hits": $(top_first_body) },
        "last":  { "top_hits": $(top_last_body) }
      }
    }
  }
}
EOF
)"

# =============================================================================
# Execute Global Metrics Query
# =============================================================================
global_metrics_cmd="$(cat <<'EOF'
curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
  -H 'Content-Type: application/json' \
  '__URL__' -d @- <<'JSON'
__BODY__
JSON
EOF
)"
global_metrics_cmd="${global_metrics_cmd//__URL__/$ES_URL/$idx/_search?size=0}"
global_metrics_cmd="${global_metrics_cmd//__BODY__/$global_metrics_body}"

if [[ "$PRINT_QUERY_CMD" == "1" ]]; then
  echo "[elk_performance_test] Global Metrics query command:"
  printf '%s\n' "$global_metrics_cmd"
fi

metrics_json="$(eval "$global_metrics_cmd")"

read_count="$(echo "$metrics_json" | jq -r '.aggregations.reads.doc_count // 0')"
background_read_count="$(echo "$metrics_json" | jq -r '.aggregations.background_reads.doc_count // 0')"

write_count="$(echo "$metrics_json" | jq -r '.aggregations.writes.doc_count // 0')"
background_write_count="$(echo "$metrics_json" | jq -r '.aggregations.background_writes.doc_count // 0')"

logical_write_count="$(echo "$metrics_json" | jq -r '.aggregations.logical_writes.doc_count // 0')"

garbage_collection_count="$(echo "$metrics_json" | jq -r '.aggregations.gcs.doc_count // 0')"
background_garbage_collection_count="$(echo "$metrics_json" | jq -r '.aggregations.background_gcs.doc_count // 0')"

foreground_gc_count=$((garbage_collection_count - background_garbage_collection_count))
(( foreground_gc_count < 0 )) && foreground_gc_count=0

get_first_ts() { jq -r --arg f "$TS_FIELD" "$1" <<<"$metrics_json"; }
get_last_ts()  { jq -r --arg f "$TS_FIELD" "$1" <<<"$metrics_json"; }

read_first_ts="$(get_first_ts '.aggregations.reads.first.hits.hits[0]._source[$f] // empty')"
read_last_ts="$(get_last_ts  '.aggregations.reads.last.hits.hits[0]._source[$f] // empty')"

write_first_ts="$(get_first_ts '.aggregations.writes.first.hits.hits[0]._source[$f] // empty')"
write_last_ts="$(get_last_ts  '.aggregations.writes.last.hits.hits[0]._source[$f] // empty')"

gc_first_ts="$(get_first_ts '.aggregations.gcs.first.hits.hits[0]._source[$f] // empty')"
gc_last_ts="$(get_last_ts  '.aggregations.gcs.last.hits.hits[0]._source[$f] // empty')"

activity_first_ts="$(get_first_ts '.aggregations.activity.first.hits.hits[0]._source[$f] // empty')"
activity_last_ts="$(get_last_ts  '.aggregations.activity.last.hits.hits[0]._source[$f] // empty')"

ts_to_epoch() {
  local ts="$1"
  [[ -z "${ts:-}" ]] && { echo ""; return; }
  if [[ "$TS_FIELD" == "@timestamp" ]]; then
    date -u -d "$ts" +%s 2>/dev/null || echo ""
  else
    local iso
    iso="$(echo "$ts" | sed -E 's/_/T/; s/T([0-9]{2})-([0-9]{2})-([0-9]{2})/T\1:\2:\3/; s/\..*$//; s/$/Z/')"
    date -u -d "$iso" +%s 2>/dev/null || echo ""
  fi
}

span_secs() {
  local count="$1" a="$2" b="$3"
  awk -v c="$count" -v x="$a" -v y="$b" 'BEGIN{
    if (c<=0) { print 0; exit }
    if (x=="" || y=="") { print 0; exit }
    d=y-x;
    if (d<1) d=1;
    print d
  }'
}

read_first_epoch="$(ts_to_epoch "$read_first_ts")"
read_last_epoch="$(ts_to_epoch "$read_last_ts")"
write_first_epoch="$(ts_to_epoch "$write_first_ts")"
write_last_epoch="$(ts_to_epoch "$write_last_ts")"
gc_first_epoch="$(ts_to_epoch "$gc_first_ts")"
gc_last_epoch="$(ts_to_epoch "$gc_last_ts")"
activity_first_epoch="$(ts_to_epoch "$activity_first_ts")"
activity_last_epoch="$(ts_to_epoch "$activity_last_ts")"

read_span_secs="$(span_secs "$read_count" "$read_first_epoch" "$read_last_epoch")"
write_span_secs="$(span_secs "$write_count" "$write_first_epoch" "$write_last_epoch")"
gc_span_secs="$(span_secs "$garbage_collection_count" "$gc_first_epoch" "$gc_last_epoch")"
activity_span_secs="$(span_secs "$((read_count + write_count + logical_write_count + garbage_collection_count))" "$activity_first_epoch" "$activity_last_epoch")"

read_iops="$(awk -v c="$read_count" -v d="$read_span_secs" 'BEGIN { if (d<=0) print 0; else printf "%.6f", c/d }')"
write_iops="$(awk -v c="$write_count" -v d="$write_span_secs" 'BEGIN { if (d<=0) print 0; else printf "%.6f", c/d }')"

write_amplification="$(awk -v p="$write_count" -v l="$logical_write_count" 'BEGIN { if (l<=0) print 0; else printf "%.6f", p/l }')"

gc_rate="$(awk -v c="$garbage_collection_count" -v d="$activity_span_secs" 'BEGIN { if (d<=0) print 0; else printf "%.6f", c/d }')"
foreground_gc_rate="$(awk -v c="$foreground_gc_count" -v d="$activity_span_secs" 'BEGIN { if (d<=0) print 0; else printf "%.6f", c/d }')"

util_sample_count="$(echo "$metrics_json" | jq -r '.aggregations.utilization.count.value // 0')"
util_last_src="$(echo "$metrics_json" | jq -c '.aggregations.utilization.last.hits.hits[0]._source // {}')"
util_max_src="$(echo "$metrics_json" | jq -c '.aggregations.utilization.max_occ.hits.hits[0]._source // {}')"
util_min_src="$(echo "$metrics_json" | jq -c '.aggregations.utilization.min_occ.hits.hits[0]._source // {}')"

util_from_src() {
  local src="$1"
  local u tp op

  u="$(echo "$src" | jq -r '.utilization_percent // empty')"
  tp="$(echo "$src" | jq -r '.total_pages // empty')"
  op="$(echo "$src" | jq -r '.occupied_pages // empty')"

  if [[ -n "${u:-}" ]]; then
    awk -v x="$u" 'BEGIN{ if (x > 1.0) printf "%.12f", x/100.0; else printf "%.12f", x }'
    return
  fi

  if [[ -n "${tp:-}" && -n "${op:-}" ]]; then
    awk -v o="$op" -v t="$tp" 'BEGIN{ if (t<=0) print ""; else printf "%.12f", o/t }'
    return
  fi

  echo ""
}

utilization_last="$(util_from_src "$util_last_src")"
utilization_max="$(util_from_src "$util_max_src")"
utilization_min="$(util_from_src "$util_min_src")"

utilization_avg_raw="$(echo "$metrics_json" | jq -r '.aggregations.utilization.avg_util.value // empty')"
utilization_avg=""
if [[ -n "${utilization_avg_raw:-}" ]]; then
  utilization_avg="$(awk -v x="$utilization_avg_raw" 'BEGIN{ if (x > 1.0) printf "%.12f", x/100.0; else printf "%.12f", x }')"
fi

total_pages="$(echo "$util_last_src" | jq -r '.total_pages // empty')"
ssd_size_bytes="$(echo "$util_last_src" | jq -r '."test.ssd.size" // empty')"

page_size_bytes=""
if [[ -n "${ssd_size_bytes:-}" && -n "${total_pages:-}" ]]; then
  page_size_bytes="$(awk -v s="$ssd_size_bytes" -v t="$total_pages" 'BEGIN{ if (t<=0) print ""; else printf "%.12f", s/t }')"
fi

read_speed_mbps="-"
write_speed_mbps="-"
if [[ -n "${page_size_bytes:-}" ]]; then
  read_speed_mbps="$(awk -v c="$read_count" -v p="$page_size_bytes" -v d="$read_span_secs" \
    'BEGIN{ if (d<=0) print 0; else printf "%.6f", (c*p*8.0)/(d*1000000.0) }')"
  write_speed_mbps="$(awk -v c="$write_count" -v p="$page_size_bytes" -v d="$write_span_secs" \
    'BEGIN{ if (d<=0) print 0; else printf "%.6f", (c*p*8.0)/(d*1000000.0) }')"
fi

fmt_pct() {
  local u="$1"
  [[ -z "${u:-}" ]] && { echo "UNKNOWN"; return; }
  awk -v x="$u" 'BEGIN{ printf "%.4f", x*100.0 }'
}

echo "======================================================================="
echo "[elk_performance_test] GLOBAL PERFORMANCE METRICS"
echo "======================================================================="
echo "[elk_performance_test] Monitor window: FROM=$FROM_DATE TO=$TO_DATE"
echo "[elk_performance_test] Metrics window: FROM=$METRICS_FROM_DATE TO=$TO_DATE"
echo "[elk_performance_test] Monitor duration: ${INTERVAL_SECS}s TS_FIELD=$TS_FIELD index=$idx"
echo "[elk_performance_test] Activity duration: ${activity_span_secs}s"
echo ""
echo "[elk_performance_test] Physical writes: $write_count (background: $background_write_count) span=${write_span_secs}s"
echo "[elk_performance_test] Physical reads:  $read_count (background: $background_read_count) span=${read_span_secs}s"
echo "[elk_performance_test] Logical writes:  $logical_write_count"
echo "[elk_performance_test] Garbage collections: $garbage_collection_count (background: $background_garbage_collection_count, foreground: $foreground_gc_count) span=${gc_span_secs}s"
echo ""
echo "[elk_performance_test] Read IOPS:  $read_iops"
echo "[elk_performance_test] Write IOPS: $write_iops"
echo "[elk_performance_test] Read speed:  $read_speed_mbps Mb/s"
echo "[elk_performance_test] Write speed: $write_speed_mbps Mb/s"
echo "[elk_performance_test] Write amplification: $write_amplification"
echo "[elk_performance_test] GC rate: $gc_rate events/sec (foreground: $foreground_gc_rate)"
echo ""
echo "[elk_performance_test] Utilization samples: $util_sample_count"
echo "[elk_performance_test] Utilization last: ${utilization_last:-UNKNOWN} ($(fmt_pct "${utilization_last:-}")%)"
echo "[elk_performance_test] Utilization avg: ${utilization_avg:-UNKNOWN} ($(fmt_pct "${utilization_avg:-}")%)"
echo "[elk_performance_test] Utilization max:  ${utilization_max:-UNKNOWN} ($(fmt_pct "${utilization_max:-}")%)"
echo "[elk_performance_test] Utilization min:  ${utilization_min:-UNKNOWN} ($(fmt_pct "${utilization_min:-}")%)"
echo "[elk_performance_test] Page size: ${page_size_bytes:-UNKNOWN} bytes"
echo "======================================================================="

test_fail=0
echo ""
echo "[elk_performance_test] === GLOBAL PERFORMANCE CHECKS ==="

if (( p95 > P95_LIMIT_MS )); then
  echo "[elk_performance_test] FAIL: P95 latency (${p95}ms) > limit (${P95_LIMIT_MS}ms)"
  test_fail=1
else
  echo "[elk_performance_test] OK: P95 latency (${p95}ms) <= limit (${P95_LIMIT_MS}ms)"
fi

if (( read_count < MIN_READ_COUNT )); then
  echo "[elk_performance_test] FAIL: read_count ($read_count) < MIN_READ_COUNT ($MIN_READ_COUNT)"
  test_fail=1
else
  echo "[elk_performance_test] OK: read_count ($read_count) >= MIN_READ_COUNT ($MIN_READ_COUNT)"
fi

if (( write_count < MIN_WRITE_COUNT )); then
  echo "[elk_performance_test] FAIL: write_count ($write_count) < MIN_WRITE_COUNT ($MIN_WRITE_COUNT)"
  test_fail=1
else
  echo "[elk_performance_test] OK: write_count ($write_count) >= MIN_WRITE_COUNT ($MIN_WRITE_COUNT)"
fi

if (( logical_write_count < MIN_LOGICAL_WRITE_COUNT )); then
  echo "[elk_performance_test] FAIL: logical_write_count ($logical_write_count) < MIN_LOGICAL_WRITE_COUNT ($MIN_LOGICAL_WRITE_COUNT)"
  test_fail=1
else
  echo "[elk_performance_test] OK: logical_write_count ($logical_write_count) >= MIN_LOGICAL_WRITE_COUNT ($MIN_LOGICAL_WRITE_COUNT)"
fi

if (( read_count > 0 )) && (( read_span_secs <= 0 )); then
  echo "[elk_performance_test] FAIL: cannot compute read activity span (missing or unparsable timestamps)"
  test_fail=1
fi

if (( write_count > 0 )) && (( write_span_secs <= 0 )); then
  echo "[elk_performance_test] FAIL: cannot compute write activity span (missing or unparsable timestamps)"
  test_fail=1
fi

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

if (( logical_write_count > 0 )); then
  if lt "$write_amplification" "$MIN_WA"; then
    echo "[elk_performance_test] FAIL: write amplification ($write_amplification) < MIN_WA ($MIN_WA)"
    test_fail=1
  else
    echo "[elk_performance_test] OK: write amplification ($write_amplification) >= MIN_WA ($MIN_WA)"
  fi

  if gt "$write_amplification" "$MAX_WA"; then
    echo "[elk_performance_test] FAIL: write amplification ($write_amplification) > MAX_WA ($MAX_WA)"
    test_fail=1
  else
    echo "[elk_performance_test] OK: write amplification ($write_amplification) <= MAX_WA ($MAX_WA)"
  fi
else
  echo "[elk_performance_test] SKIP: write amplification (no logical writes recorded)"
fi

if [[ "$read_speed_mbps" != "-" ]]; then
  if lt "$read_speed_mbps" "$MIN_READ_SPEED_MBPS"; then
    echo "[elk_performance_test] FAIL: read speed ($read_speed_mbps Mb/s) < MIN_READ_SPEED_MBPS ($MIN_READ_SPEED_MBPS Mb/s)"
    test_fail=1
  else
    echo "[elk_performance_test] OK: read speed ($read_speed_mbps Mb/s) >= MIN_READ_SPEED_MBPS ($MIN_READ_SPEED_MBPS Mb/s)"
  fi
else
  echo "[elk_performance_test] SKIP: read speed (page size unknown)"
fi

if [[ "$write_speed_mbps" != "-" ]]; then
  if lt "$write_speed_mbps" "$MIN_WRITE_SPEED_MBPS"; then
    echo "[elk_performance_test] FAIL: write speed ($write_speed_mbps Mb/s) < MIN_WRITE_SPEED_MBPS ($MIN_WRITE_SPEED_MBPS Mb/s)"
    test_fail=1
  else
    echo "[elk_performance_test] OK: write speed ($write_speed_mbps Mb/s) >= MIN_WRITE_SPEED_MBPS ($MIN_WRITE_SPEED_MBPS Mb/s)"
  fi
else
  echo "[elk_performance_test] SKIP: write speed (page size unknown)"
fi

if gt "$gc_rate" "$MAX_GC_RATE"; then
  echo "[elk_performance_test] FAIL: GC rate ($gc_rate events/sec) > MAX_GC_RATE ($MAX_GC_RATE)"
  test_fail=1
else
  echo "[elk_performance_test] OK: GC rate ($gc_rate events/sec) <= MAX_GC_RATE ($MAX_GC_RATE)"
fi

if (( REQUIRE_UTILIZATION == 1 )) && (( util_sample_count == 0 )); then
  echo "[elk_performance_test] FAIL: no utilization samples in window (SsdUtilizationLog missing)"
  test_fail=1
fi

if (( foreground_gc_count >= GC_FOREGROUND_TRIGGER )); then
  if [[ -z "${utilization_max:-}" ]]; then
    echo "[elk_performance_test] FAIL: foreground GC occurred but utilization_max is missing"
    test_fail=1
  else
    if lt "$utilization_max" "$MIN_UTIL_AT_FG_GC"; then
      echo "[elk_performance_test] FAIL: foreground GC occurred but utilization_max ($utilization_max) < MIN_UTIL_AT_FG_GC ($MIN_UTIL_AT_FG_GC)"
      test_fail=1
    else
      echo "[elk_performance_test] OK: foreground GC occurred and utilization_max ($utilization_max) >= MIN_UTIL_AT_FG_GC ($MIN_UTIL_AT_FG_GC)"
    fi
  fi
else
  echo "[elk_performance_test] OK: foreground GC count ($foreground_gc_count) < GC_FOREGROUND_TRIGGER ($GC_FOREGROUND_TRIGGER), skipping FG utilization check"
fi

# =============================================================================
# Execute Sector Test Metrics Query
# =============================================================================
echo ""
echo "[elk_performance_test] === SECTOR TEST CHECKS ($TEST_NAME_FILTER) ==="

sector_metrics_cmd="$(cat <<'EOF'
curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
  -H 'Content-Type: application/json' \
  '__URL__' -d @- <<'JSON'
__BODY__
JSON
EOF
)"
sector_metrics_cmd="${sector_metrics_cmd//__URL__/$ES_URL/$idx/_search?size=0}"
sector_metrics_cmd="${sector_metrics_cmd//__BODY__/$sector_metrics_body}"

if [[ "$PRINT_QUERY_CMD" == "1" ]]; then
  echo "[elk_performance_test] Sector Metrics query command:"
  printf '%s\n' "$sector_metrics_cmd"
fi

sector_metrics_json="$(eval "$sector_metrics_cmd")"

sector_read_count="$(echo "$sector_metrics_json" | jq -r '.aggregations.reads.doc_count // 0')"
sector_write_count="$(echo "$sector_metrics_json" | jq -r '.aggregations.writes.doc_count // 0')"
sector_logical_write_count="$(echo "$sector_metrics_json" | jq -r '.aggregations.logical_writes.doc_count // 0')"

get_sector_first_ts() { jq -r --arg f "$TS_FIELD" "$1" <<<"$sector_metrics_json"; }
get_sector_last_ts()  { jq -r --arg f "$TS_FIELD" "$1" <<<"$sector_metrics_json"; }

sector_read_first_ts="$(get_sector_first_ts '.aggregations.reads.first.hits.hits[0]._source[$f] // empty')"
sector_read_last_ts="$(get_sector_last_ts  '.aggregations.reads.last.hits.hits[0]._source[$f] // empty')"
sector_write_first_ts="$(get_sector_first_ts '.aggregations.writes.first.hits.hits[0]._source[$f] // empty')"
sector_write_last_ts="$(get_sector_last_ts  '.aggregations.writes.last.hits.hits[0]._source[$f] // empty')"

sector_read_first_epoch="$(ts_to_epoch "$sector_read_first_ts")"
sector_read_last_epoch="$(ts_to_epoch "$sector_read_last_ts")"
sector_write_first_epoch="$(ts_to_epoch "$sector_write_first_ts")"
sector_write_last_epoch="$(ts_to_epoch "$sector_write_last_ts")"

sector_read_span_secs="$(span_secs "$sector_read_count" "$sector_read_first_epoch" "$sector_read_last_epoch")"
sector_write_span_secs="$(span_secs "$sector_write_count" "$sector_write_first_epoch" "$sector_write_last_epoch")"

sector_read_iops="$(awk -v c="$sector_read_count" -v d="$sector_read_span_secs" 'BEGIN { if (d<=0) print 0; else printf "%.6f", c/d }')"
sector_write_iops="$(awk -v c="$sector_write_count" -v d="$sector_write_span_secs" 'BEGIN { if (d<=0) print 0; else printf "%.6f", c/d }')"

sector_write_amplification="$(awk -v p="$sector_write_count" -v l="$sector_logical_write_count" 'BEGIN { if (l<=0) print 0; else printf "%.6f", p/l }')"

sector_read_speed_mbps="-"
sector_write_speed_mbps="-"
if [[ -n "${page_size_bytes:-}" ]]; then
  sector_read_speed_mbps="$(awk -v c="$sector_read_count" -v p="$page_size_bytes" -v d="$sector_read_span_secs" \
    'BEGIN{ if (d<=0) print 0; else printf "%.6f", (c*p*8.0)/(d*1000000.0) }')"
  sector_write_speed_mbps="$(awk -v c="$sector_write_count" -v p="$page_size_bytes" -v d="$sector_write_span_secs" \
    'BEGIN{ if (d<=0) print 0; else printf "%.6f", (c*p*8.0)/(d*1000000.0) }')"
fi

echo "[elk_performance_test] Sector writes: $sector_write_count span=${sector_write_span_secs}s"
echo "[elk_performance_test] Sector reads:  $sector_read_count span=${sector_read_span_secs}s"
echo "[elk_performance_test] Sector logical writes: $sector_logical_write_count"
echo "[elk_performance_test] Sector write IOPS: $sector_write_iops"
echo "[elk_performance_test] Sector read IOPS:  $sector_read_iops"
echo "[elk_performance_test] Sector write speed: $sector_write_speed_mbps Mb/s"
echo "[elk_performance_test] Sector read speed:  $sector_read_speed_mbps Mb/s"
echo "[elk_performance_test] Sector write amplification: $sector_write_amplification"

if (( sector_write_count < SECTOR_MIN_WRITE_COUNT )); then
  echo "[elk_performance_test] FAIL: sector write_count ($sector_write_count) < SECTOR_MIN_WRITE_COUNT ($SECTOR_MIN_WRITE_COUNT)"
  test_fail=1
else
  echo "[elk_performance_test] OK: sector write_count ($sector_write_count) >= SECTOR_MIN_WRITE_COUNT ($SECTOR_MIN_WRITE_COUNT)"
fi

if (( sector_read_count > 0 )); then
  if (( sector_read_count < SECTOR_MIN_READ_COUNT )); then
    echo "[elk_performance_test] FAIL: sector read_count ($sector_read_count) < SECTOR_MIN_READ_COUNT ($SECTOR_MIN_READ_COUNT)"
    test_fail=1
  else
    echo "[elk_performance_test] OK: sector read_count ($sector_read_count) >= SECTOR_MIN_READ_COUNT ($SECTOR_MIN_READ_COUNT)"
  fi
fi

if lt "$sector_write_iops" "$SECTOR_MIN_IOPS_WRITE"; then
  echo "[elk_performance_test] FAIL: sector write IOPS ($sector_write_iops) < SECTOR_MIN_IOPS_WRITE ($SECTOR_MIN_IOPS_WRITE)"
  test_fail=1
else
  echo "[elk_performance_test] OK: sector write IOPS ($sector_write_iops) >= SECTOR_MIN_IOPS_WRITE ($SECTOR_MIN_IOPS_WRITE)"
fi

if (( sector_read_count > 0 )); then
  if lt "$sector_read_iops" "$SECTOR_MIN_IOPS_READ"; then
    echo "[elk_performance_test] FAIL: sector read IOPS ($sector_read_iops) < SECTOR_MIN_IOPS_READ ($SECTOR_MIN_IOPS_READ)"
    test_fail=1
  else
    echo "[elk_performance_test] OK: sector read IOPS ($sector_read_iops) >= SECTOR_MIN_IOPS_READ ($SECTOR_MIN_IOPS_READ)"
  fi
fi

if [[ "$sector_write_speed_mbps" != "0" ]] && [[ "$sector_write_speed_mbps" != "-" ]]; then
  if lt "$sector_write_speed_mbps" "$SECTOR_MIN_WRITE_SPEED_MBPS"; then
    echo "[elk_performance_test] FAIL: sector write speed ($sector_write_speed_mbps Mb/s) < SECTOR_MIN_WRITE_SPEED_MBPS ($SECTOR_MIN_WRITE_SPEED_MBPS Mb/s)"
    test_fail=1
  else
    echo "[elk_performance_test] OK: sector write speed ($sector_write_speed_mbps Mb/s) >= SECTOR_MIN_WRITE_SPEED_MBPS ($SECTOR_MIN_WRITE_SPEED_MBPS Mb/s)"
  fi
else
  echo "[elk_performance_test] SKIP: sector write speed (not available)"
fi

if (( sector_read_count > 0 )) && [[ "$sector_read_speed_mbps" != "0" ]] && [[ "$sector_read_speed_mbps" != "-" ]]; then
  if lt "$sector_read_speed_mbps" "$SECTOR_MIN_READ_SPEED_MBPS"; then
    echo "[elk_performance_test] FAIL: sector read speed ($sector_read_speed_mbps Mb/s) < SECTOR_MIN_READ_SPEED_MBPS ($SECTOR_MIN_READ_SPEED_MBPS Mb/s)"
    test_fail=1
  else
    echo "[elk_performance_test] OK: sector read speed ($sector_read_speed_mbps Mb/s) >= SECTOR_MIN_READ_SPEED_MBPS ($SECTOR_MIN_READ_SPEED_MBPS Mb/s)"
  fi
fi

if (( sector_logical_write_count > 0 )); then
  if lt "$sector_write_amplification" "$SECTOR_MIN_WA"; then
    echo "[elk_performance_test] FAIL: sector write amplification ($sector_write_amplification) < SECTOR_MIN_WA ($SECTOR_MIN_WA)"
    test_fail=1
  else
    echo "[elk_performance_test] OK: sector write amplification ($sector_write_amplification) >= SECTOR_MIN_WA ($SECTOR_MIN_WA)"
  fi

  if gt "$sector_write_amplification" "$SECTOR_MAX_WA"; then
    echo "[elk_performance_test] FAIL: sector write amplification ($sector_write_amplification) > SECTOR_MAX_WA ($SECTOR_MAX_WA)"
    test_fail=1
  else
    echo "[elk_performance_test] OK: sector write amplification ($sector_write_amplification) <= SECTOR_MAX_WA ($SECTOR_MAX_WA)"
  fi
else
  echo "[elk_performance_test] SKIP: sector write amplification (no logical writes recorded)"
fi

if (( test_fail == 1 )); then
  echo "[elk_performance_test] METRICS TEST FAILED"
  exit 1
fi

echo "[elk_performance_test] PERFORMANCE OK"
