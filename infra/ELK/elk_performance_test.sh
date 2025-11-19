#!/bin/bash
# ELK performance test:
# - uses elastic/$ELASTIC_PASSWORD
# - queries first "disk" index (filebeat-*, else first index)
# - prints sample record
# - fails if P95 latency is above threshold

set -euo pipefail
trap 'ec=$?; echo "[elk_performance_test] FAILED (exit $ec) on: $BASH_COMMAND" >&2' ERR

ELK_DIR="${1:-$(pwd)}"
ES_URL="${ES_URL:-https://localhost:9200}"
INDEX_PATTERN="${INDEX_PATTERN:-filebeat-*}"
SAMPLES="${SAMPLES:-30}"
P95_LIMIT_MS="${P95_LIMIT_MS:-500}"
WAIT_SECS="${WAIT_SECS:-60}"

cd "$ELK_DIR"

# Load .env if present and ELASTIC_PASSWORD not set
if [[ -f .env && -z "${ELASTIC_PASSWORD:-}" ]]; then
  set -o allexport
  source .env
  set +o allexport
fi
: "${ELASTIC_PASSWORD:?ELASTIC_PASSWORD must be set or present in .env}"

echo "[elk_performance_test] ES_URL=$ES_URL INDEX_PATTERN=$INDEX_PATTERN SAMPLES=$SAMPLES P95_LIMIT_MS=$P95_LIMIT_MS"

# ---- Seconds the Miliseconds ----
sec_to_ms() {
  local s="$1"
  local int_part="${s%%.*}"
  echo $((int_part * 1000))
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

# ---- print a 1 record so we see it actually works ----
echo "[elk_performance_test] fetching sample records from $idx..."
sample_json=$(curl -k -s -u "elastic:$ELASTIC_PASSWORD" \
              "$ES_URL/$idx/_search?q=*&size=1")
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
  t_sec=$(curl -k -s -o /dev/null -w '%{time_total}' \
          -u "elastic:$ELASTIC_PASSWORD" \
          "$ES_URL/$idx/_search?q=*")
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

echo "[elk_performance_test] PERFORMANCE OK"
