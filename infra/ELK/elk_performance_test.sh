#!/bin/bash
# ELK performance test:
# - uses elastic/$ELASTIC_PASSWORD
# - queries first "disk" index (filebeat-*, else first index)
# - prints sample record
# - fails if P95 latency is above threshold
# - checks IOPS and Write Amplification vs thresholds

set -euo pipefail
trap 'ec=$?; echo "[elk_performance_test] FAILED (exit $ec) on: $BASH_COMMAND" >&2' ERR

ELK_DIR="${1:-$(pwd)}"
ES_URL="${ES_URL:-https://localhost:9200}"
INDEX_PATTERN="${INDEX_PATTERN:-filebeat-*}"
SAMPLES="${SAMPLES:-30}"
P95_LIMIT_MS="${P95_LIMIT_MS:-500}"
MIN_IOPS_READ="${MIN_IOPS_READ:-0.1}"
MIN_IOPS_WRITE="${MIN_IOPS_WRITE:-0.5}"
MAX_WA="${MAX_WA:-2.0}"
WAIT_SECS="${WAIT_SECS:-60}"
LOOKBACK_HOURS="${LOOKBACK_HOURS:-1}"
FILEBEAT_WAIT_SECS="${FILEBEAT_WAIT_SECS:-120}"
FROM_DATE="$(date -u -d "${LOOKBACK_HOURS} hours ago" +%Y-%m-%dT%H:%M:%SZ)"
TO_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
QUERY_STRING="@timestamp:[$FROM_DATE+TO+$TO_DATE]"

cd "$ELK_DIR"

# Load .env if present and ELASTIC_PASSWORD not set
if [[ -f .env && -z "${ELASTIC_PASSWORD:-}" ]]; then
  set -o allexport
  source .env
  set +o allexport
fi
: "${ELASTIC_PASSWORD:?ELASTIC_PASSWORD must be set or present in .env}"

# Disable Proxies
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY

echo "[elk_performance_test] ES_URL=$ES_URL INDEX_PATTERN=$INDEX_PATTERN SAMPLES=$SAMPLES P95_LIMIT_MS=$P95_LIMIT_MS"
echo "[elk_performance_test] thresholds: MIN_IOPS_READ=$MIN_IOPS_READ MIN_IOPS_WRITE=$MIN_IOPS_WRITE MAX_WA=$MAX_WA"
echo "[elk_performance_test] time window: LOOKBACK_HOURS=$LOOKBACK_HOURS FILEBEAT_WAIT_SECS=$FILEBEAT_WAIT_SECS"

# ---- Seconds to Milliseconds (keeps sub-second values) ----
sec_to_ms() {
  local s="$1"
  awk -v v="$s" 'BEGIN {
    if (v == "" || v == "-") { print 0; exit }
    printf "%d\n", v * 1000
  }'
}
# -------------------------------------------------------------------

# Wait for ES
deadline=$((SECONDS + WAIT_SECS))
while (( SECONDS < deadline )); do
  if curl -k -s -u "elastic:$ELASTIC_PASSWORD" "$ES_URL/_cluster/health" >/dev/null 2>&1; then
    echo "[elk_performance_test] Elasticsearch is up"
    break
  fi
  echo "[elk_performance_test] waiting for ES..."
  sleep 3
done

# First disk index: filebeat-* then fallback
get_first_index() {
  local url="$1"
  local indices
  indices="$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" "$url" || true)"
  # first non-empty line
  while IFS= read -r line; do
    if [[ -n "$line" ]]; then
      echo "$line"
      return
    fi
  done <<< "$indices"
}

idx="$(get_first_index "$ES_URL/_cat/indices/$INDEX_PATTERN?h=index")"
if [[ -z "$idx" ]]; then
  idx="$(get_first_index "$ES_URL/_cat/indices?h=index")"
fi
[[ -n "$idx" ]] || { echo "[elk_performance_test] no indices found"; exit 1; }

echo "[elk_performance_test] using disk index: $idx"

# Wait for Filebeat to finish indexing logs
echo "[elk_performance_test] waiting for Filebeat to index logs (up to ${FILEBEAT_WAIT_SECS}s)..."
deadline=$((SECONDS + FILEBEAT_WAIT_SECS))
start_time=$SECONDS
data_found=0
while (( SECONDS < deadline )); do
  # Check if there's any data in the index within the time window
  count_json=$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
    -H 'Content-Type: application/json' \
    "$ES_URL/$idx/_count" -d @- <<EOF
{
  "query": {
    "range": {
      "@timestamp": {
        "gte": "$FROM_DATE",
        "lte": "$TO_DATE"
      }
    }
  }
}
EOF
  )

  if command -v jq >/dev/null 2>&1; then
    count=$(echo "$count_json" | jq -r '.count // 0')
  else
    # Fallback: extract count using grep/sed if jq is not available
    count=$(echo "$count_json" | grep -o '"count":[0-9]*' | grep -o '[0-9]*' || echo "0")
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

if [[ "$data_found" -eq 0 ]]; then
  echo "[elk_performance_test] WARNING: no data found in index $idx within time window after ${FILEBEAT_WAIT_SECS}s"
  echo "[elk_performance_test] time window: FROM=$FROM_DATE TO=$TO_DATE"
  echo "[elk_performance_test] checking total document count in index..."
  total_count_json=$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" "$ES_URL/$idx/_count")
  if command -v jq >/dev/null 2>&1; then
    total_count=$(echo "$total_count_json" | jq -r '.count // 0')
  else
    total_count=$(echo "$total_count_json" | grep -o '"count":[0-9]*' | grep -o '[0-9]*' || echo "0")
  fi
  echo "[elk_performance_test] total documents in index: $total_count"

  # If there's data in the index but not in our time window, try a wider window
  if [[ "$total_count" -gt 0 ]]; then
    echo "[elk_performance_test] trying wider time window (last 24 hours)..."
    FROM_DATE="$(date -u -d '24 hours ago' +%Y-%m-%dT%H:%M:%SZ)"
    QUERY_STRING="@timestamp:[$FROM_DATE+TO+$TO_DATE]"
    echo "[elk_performance_test] new time window: FROM=$FROM_DATE TO=$TO_DATE"
  else
    echo "[elk_performance_test] ERROR: index is empty, Filebeat may not be shipping logs"
    exit 1
  fi
fi

echo "-----------------------------------------------------------------------"
echo "[elk_performance_test] You can run these queries manually in Kibana Dev Tools:"
cat <<EOF
# Sample document query (same time window as the test)
GET $idx/_search
{
  "size": 1,
  "query": {
    "range": {
      "@timestamp": {
        "gte": "$FROM_DATE",
        "lte": "$TO_DATE"
      }
    }
  }
}

# Metrics aggregation query (reads / physical writes / logical writes)
GET $idx/_search
{
  "size": 0,
  "query": {
    "range": {
      "@timestamp": {
        "gte": "$FROM_DATE",
        "lte": "$TO_DATE"
      }
    }
  },
  "aggs": {
    "reads": {
      "filter": { "term": { "type": "PhysicalCellReadLog" } }
    },
    "phys_writes": {
      "filter": { "term": { "type": "PhysicalCellProgramLog" } }
    },
    "log_writes": {
      "filter": { "term": { "type": "LogicalCellProgramLog" } }
    }
  }
}
EOF

echo
echo "[elk_performance_test] Or via curl (sample doc):"
echo "curl -g -k -u \"elastic:\$ELASTIC_PASSWORD\" \"$ES_URL/$idx/_search?q=$QUERY_STRING&size=1&pretty\""
echo "-----------------------------------------------------------------------"

# ---- print a 1 record so we see it actually works ----
echo "[elk_performance_test] fetching sample records from $idx..."
sample_json=$(curl -g -k -s -u "elastic:$ELASTIC_PASSWORD" \
              "$ES_URL/$idx/_search?q=$QUERY_STRING&size=1")
if command -v jq >/dev/null 2>&1; then
  echo "$sample_json" | jq '.hits.hits[]._source'
else
  echo "$sample_json"
fi
echo "[elk_performance_test] end of sample records"
# ---------------------------------------------------------

lat_file="$(mktemp)"
trap 'rm -f "$lat_file"' EXIT

# collect latencies
i=1
while [ "$i" -le "$SAMPLES" ]; do
  t_sec=$(curl -g -k -s -o /dev/null -w '%{time_total}' \
          -u "elastic:$ELASTIC_PASSWORD" \
          "$ES_URL/$idx/_search?q=$QUERY_STRING")
  ms=$(sec_to_ms "$t_sec")
  echo "$ms" >>"$lat_file"
  i=$((i + 1))
done

n=$(wc -l <"$lat_file")
[[ "$n" -gt 0 ]] || { echo "[elk_performance_test] no samples collected"; exit 1; }

# P95 index: ceil(0.95 * n)
p95_idx=$(((95 * n + 99) / 100))
if (( p95_idx < 1 )); then p95_idx=1; fi
if (( p95_idx > n )); then p95_idx="$n"; fi

p95=$(sort -n "$lat_file" | sed -n "${p95_idx}p")

echo "[elk_performance_test] samples=$n P95=${p95}ms limit=${P95_LIMIT_MS}ms"

if (( p95 > P95_LIMIT_MS )); then
  echo "[elk_performance_test] PERFORMANCE FAIL (P95 ${p95}ms > ${P95_LIMIT_MS}ms)"
  exit 1
fi

# -------------------------------------------------------------------
# IOPS & Write Amplification checks based on log counts
# -------------------------------------------------------------------
if ! command -v jq >/dev/null 2>&1; then
  echo "[elk_performance_test] jq is required for IOPS/WA checks"
  exit 1
fi

# Compute interval length in seconds from FROM_DATE/TO_DATE
FROM_EPOCH=$(date -u -d "$FROM_DATE" +%s)
TO_EPOCH=$(date -u -d "$TO_DATE" +%s)
INTERVAL_SECS=$((TO_EPOCH - FROM_EPOCH))
if (( INTERVAL_SECS <= 0 )); then
  echo "[elk_performance_test] invalid time window: INTERVAL_SECS=$INTERVAL_SECS"
  exit 1
fi

metrics_json=$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
  -H 'Content-Type: application/json' \
  "$ES_URL/$idx/_search?size=0" -d @- <<EOF
{
  "query": {
    "range": {
      "@timestamp": {
        "gte": "$FROM_DATE",
        "lte": "$TO_DATE"
      }
    }
  },
  "aggs": {
    "reads": {
      "filter": {
        "term": { "type": "PhysicalCellReadLog" }
      }
    },
    "phys_writes": {
      "filter": {
        "term": { "type": "PhysicalCellProgramLog" }
      }
    },
    "log_writes": {
      "filter": {
        "term": {
          "type": "LogicalCellProgramLog"
        }
      }
    }
  }
}
EOF
)

reads=$(echo "$metrics_json"       | jq -r '.aggregations.reads.doc_count       // 0')
phys_writes=$(echo "$metrics_json" | jq -r '.aggregations.phys_writes.doc_count // 0')
log_writes=$(echo "$metrics_json"  | jq -r '.aggregations.log_writes.doc_count  // 0')

read_iops=$(awk -v c="$reads" -v d="$INTERVAL_SECS" 'BEGIN { if (d<=0) print 0; else printf "%.6f", c/d }')
write_iops=$(awk -v c="$phys_writes" -v d="$INTERVAL_SECS" 'BEGIN { if (d<=0) print 0; else printf "%.6f", c/d }')
wa=$(awk -v p="$phys_writes" -v l="$log_writes" 'BEGIN { if (l<=0) print 0; else printf "%.6f", p/l }')

echo "[elk_performance_test] monitor window: FROM=$FROM_DATE TO=$TO_DATE (secs=$INTERVAL_SECS)"
echo "[elk_performance_test] reads=$reads phys_writes=$phys_writes log_writes=$log_writes"
echo "[elk_performance_test] avg_read_iops=$read_iops avg_write_iops=$write_iops wa=$wa"

# float comparison helpers: exit code 0 means condition is TRUE
lt() { awk -v v="$1" -v m="$2" 'BEGIN { exit !(v+0 < m+0) }'; }
gt() { awk -v v="$1" -v m="$2" 'BEGIN { exit !(v+0 > m+0) }'; }

test_fail=0

# Read IOPS
if lt "$read_iops" "$MIN_IOPS_READ"; then
  echo "[elk_performance_test] FAIL: read IOPS ($read_iops) < MIN_IOPS_READ ($MIN_IOPS_READ)"
  test_fail=1
else
  echo "[elk_performance_test] OK: read IOPS ($read_iops) >= MIN_IOPS_READ ($MIN_IOPS_READ)"
fi

# Write IOPS
if lt "$write_iops" "$MIN_IOPS_WRITE"; then
  echo "[elk_performance_test] FAIL: write IOPS ($write_iops) < MIN_IOPS_WRITE ($MIN_IOPS_WRITE)"
  test_fail=1
else
  echo "[elk_performance_test] OK: write IOPS ($write_iops) >= MIN_IOPS_WRITE ($MIN_IOPS_WRITE)"
fi

# Write Amplification
if gt "$wa" "$MAX_WA"; then
  echo "[elk_performance_test] FAIL: write amplification ($wa) > MAX_WA ($MAX_WA)"
  test_fail=1
else
  echo "[elk_performance_test] OK: write amplification ($wa) <= MAX_WA ($MAX_WA)"
fi

if (( test_fail == 1 )); then
  echo "[elk_performance_test] METRICS TEST FAILED"
  exit 1
fi
# -------------------------------------------------------------------

echo "[elk_performance_test] PERFORMANCE OK"
