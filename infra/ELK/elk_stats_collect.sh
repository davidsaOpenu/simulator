#!/bin/bash
set -euo pipefail

OUTPUT=""
ES_URL="${ES_URL:-https://localhost:9200}"
INDEX_PATTERN="${INDEX_PATTERN:-filebeat-*}"
LOOKBACK_HOURS="${LOOKBACK_HOURS:-24}"
ELASTIC_PASSWORD="${ELASTIC_PASSWORD:-}"
DEVICE_INDEX="${DEVICE_INDEX:-}"
SIM_PATH=""
SAMPLES="${SAMPLES:-30}"
MODE="print"


usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Collect comprehensive ELK stats from the SSD simulator.

Options:
  -o FILE     Output file (default: elk_output_stats_YYYYMMDD_HHMMSS.txt)
  -m MODE     Output mode: print|file|both (default: print)
  -u URL      Elasticsearch URL (default: https://localhost:9200)
  -i PATTERN  Index pattern (default: filebeat-*)
  -l HOURS    Lookback hours (default: 24)
  -p PASSWORD Elastic password (default: read from .env)
  -d INDEX    Filter by device_index (default: all)
  -s PATH     Simulator root path (default: auto-detect from script location)
  -h          Show this help
EOF
  exit 0
}

while getopts "o:m:u:i:l:p:d:s:h" opt; do
  case "$opt" in
    o) OUTPUT="$OPTARG" ;;
    m) MODE="$OPTARG" ;;
    u) ES_URL="$OPTARG" ;;
    i) INDEX_PATTERN="$OPTARG" ;;
    l) LOOKBACK_HOURS="$OPTARG" ;;
    p) ELASTIC_PASSWORD="$OPTARG" ;;
    d) DEVICE_INDEX="$OPTARG" ;;
    s) SIM_PATH="$OPTARG" ;;
    h) usage ;;
    *) usage ;;
  esac
done

case "$MODE" in
  print|file|both) ;;
  *) echo "$(basename "$0"): invalid mode '$MODE' (expected print, file, or both)" >&2; exit 1 ;;
esac

if [[ -z "$OUTPUT" && "$MODE" != "print" ]]; then
  OUTPUT="elk_output_stats_$(date -u +%Y%m%d_%H%M%S).txt"
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

env_file=""
if [[ -z "$SIM_PATH" ]]; then
  SIM_PATH=$SCRIPT_DIR
fi
if [[ -f "$SIM_PATH/infra/ELK/.env" ]]; then
  env_file="$SIM_PATH/infra/ELK/.env"
else
  echo "can't find env file in simulator dir: $SIM_PATH/infra/ELK/.env"
  exit 1
fi

echo "[elk_stats] loading env from: $env_file"
set -o allexport; source "$env_file"; set +o allexport
: "${ELASTIC_PASSWORD:?ELASTIC_PASSWORD must be set via -p or .env}"

unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY 2>/dev/null || true

FROM_DATE="$(date -u -d "${LOOKBACK_HOURS} hours ago" +%Y-%m-%dT%H:%M:%SZ)"
TO_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
PREFIX="[elk_stats]"

es_query() { curl -k -s -u "elastic:$ELASTIC_PASSWORD" -H 'Content-Type: application/json' "$@"; }
sec_to_ms() { awk -v v="$1" 'BEGIN{ if(v==""||v=="-"){print 0;exit} printf "%.0f\n",v*1000 }'; }
lt() { awk -v v="$1" -v m="$2" 'BEGIN{exit!(v+0<m+0)}'; }
gt() { awk -v v="$1" -v m="$2" 'BEGIN{exit!(v+0>m+0)}'; }
ts_to_epoch() { local t="$1"; [[ -z "${t:-}" ]] && { echo ""; return; }; date -u -d "$t" +%s 2>/dev/null || echo ""; }
span_secs() { awk -v c="$1" -v x="$2" -v y="$3" 'BEGIN{if(c<=0||x==""||y==""){print 0;exit}d=y-x;if(d<1)d=1;print d}'; }
section() { echo -e "\n========================================================================"; echo "# $1"; echo "========================================================================"; }

deadline=$((SECONDS + 60))
while (( SECONDS < deadline )); do
  es_query "$ES_URL/_cluster/health" >/dev/null 2>&1 && break
  sleep 3
done
es_query "$ES_URL/_cluster/health" >/dev/null 2>&1 || { echo "$PREFIX ERROR: ES not reachable"; exit 1; }
echo "$PREFIX ES is up"

idx="$(es_query "$ES_URL/_cat/indices/$INDEX_PATTERN?h=index" | awk 'NF' | sort -r | head -1)"
[[ -n "$idx" ]] || { echo "$PREFIX no indices found"; exit 1; }
echo "$PREFIX index: $idx | window: $FROM_DATE to $TO_DATE | mode: $MODE${OUTPUT:+ | output: $OUTPUT}"

tmp_out="$(mktemp)"
{
echo "# ELK Stats Snapshot"
echo "# Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "# Index: $idx"
echo "# Window: $FROM_DATE to $TO_DATE (last ${LOOKBACK_HOURS}h)"
echo "# Device filter: ${DEVICE_INDEX:-all}"

section "1. ACTIVITY TIME RANGE"
body=$(cat <<EOF
{"size":0,"query":{"bool":{"filter":[{"range":{"@timestamp":{"gte":"$FROM_DATE","lte":"$TO_DATE"}}}]}},"aggs":{"first_log":{"top_hits":{"size":1,"sort":[{"@timestamp":{"order":"asc"}}],"_source":{"includes":["@timestamp","type","device_index"]}}},"last_log":{"top_hits":{"size":1,"sort":[{"@timestamp":{"order":"desc"}}],"_source":{"includes":["@timestamp","type","device_index"]}}}}}
EOF
)
result="$(es_query "$ES_URL/$idx/_search" -d "$body")"
first_ts="$(echo "$result" | jq -r '.aggregations.first_log.hits.hits[0]._source["@timestamp"] // "n/a"' 2>/dev/null)"
first_type="$(echo "$result" | jq -r '.aggregations.first_log.hits.hits[0]._source["type"] // "n/a"' 2>/dev/null)"
last_ts="$(echo "$result" | jq -r '.aggregations.last_log.hits.hits[0]._source["@timestamp"] // "n/a"' 2>/dev/null)"
last_type="$(echo "$result" | jq -r '.aggregations.last_log.hits.hits[0]._source["type"] // "n/a"' 2>/dev/null)"
echo "First log: $first_ts ($first_type)"
echo "Last log:  $last_ts ($last_type)"
duration=0
if [[ "$first_ts" != "n/a" && "$last_ts" != "n/a" ]]; then
  e1="$(date -u -d "$first_ts" +%s 2>/dev/null || echo 0)"
  e2="$(date -u -d "$last_ts" +%s 2>/dev/null || echo 0)"
  duration=$((e2 - e1))
  echo "Activity span: ${duration}s ($(awk "BEGIN{printf \"%.1f\", $duration/3600}")h)"
fi

section "2. ALL LOG TYPE COUNTS (full index)"
body='{"size":0,"track_total_hits":true,"aggs":{"by_type":{"terms":{"field":"type","size":50}}}}'
result="$(es_query "$ES_URL/$idx/_search" -d "$body")"
echo "$result" | jq -r '.aggregations.by_type.buckets[] | "\(.key): \(.doc_count)"' 2>/dev/null || echo "$result"
total="$(echo "$result" | jq -r '.hits.total.value // "?"' 2>/dev/null || echo "?")"
echo "TOTAL_DOCUMENTS: $total"

section "3. THRESHOLD CHECKS (elk_performance_test defaults)"
check() {
  local label="$1" actual="$2" threshold="$3" op="$4"
  [[ ! "$actual" =~ ^[0-9]+$ ]] && { echo "  ??? $label: $actual (non-numeric)"; return; }
  case "$op" in
    ge) if (( actual >= threshold )); then echo "  OK   $label: $actual >= $threshold"; else echo "  FAIL $label: $actual < $threshold"; fi ;;
  esac
}
thr_body='{"size":0,"query":{"bool":{"filter":[{"range":{"@timestamp":{"gte":"'$FROM_DATE'","lte":"'$TO_DATE'"}}}]}},"aggs":{"reads":{"filter":{"term":{"type":"PhysicalCellReadLog"}}},"writes":{"filter":{"term":{"type":"PhysicalCellProgramLog"}}},"logical_writes":{"filter":{"term":{"type":"LogicalCellProgramLog"}}}}}'
thr_result="$(es_query "$ES_URL/$idx/_search" -d "$thr_body")"
thr_reads="$(echo "$thr_result" | jq -r '.aggregations.reads.doc_count // 0' 2>/dev/null || echo 0)"
thr_writes="$(echo "$thr_result" | jq -r '.aggregations.writes.doc_count // 0' 2>/dev/null || echo 0)"
thr_logical="$(echo "$thr_result" | jq -r '.aggregations.logical_writes.doc_count // 0' 2>/dev/null || echo 0)"
echo "PhysicalCellReadLog:      $thr_reads"
echo "PhysicalCellProgramLog:   $thr_writes"
echo "LogicalCellProgramLog:    $thr_logical"

read_body() { echo '{"size":0,"query":{"bool":{"filter":[{"range":{"@timestamp":{"gte":"'$FROM_DATE'","lte":"'$TO_DATE'"}}},{"term":{"type":"'"$1"'"}}]}},"aggs":{"total":{"value_count":{"field":"@timestamp"}},"by_device":{"terms":{"field":"device_index","size":10}},"by_background":{"terms":{"field":"background","size":5}},"by_channel":{"terms":{"field":"channel","size":20}},"first":{"top_hits":{"size":1,"sort":[{"@timestamp":{"order":"asc"}}],"_source":{"includes":["@timestamp","device_index","page","block","channel","background"]}}},"last":{"top_hits":{"size":1,"sort":[{"@timestamp":{"order":"desc"}}],"_source":{"includes":["@timestamp","device_index","page","block","channel","background"]}}}}}'; }

section "4. PhysicalCellReadLog BREAKDOWN (time window)"
result="$(es_query "$ES_URL/$idx/_search" -d "$(read_body PhysicalCellReadLog)")"
read_total="$(echo "$result" | jq -r '.aggregations.total.value // 0' 2>/dev/null || echo "?")"
echo "PhysicalCellReadLog total: $read_total"
echo "  by device:"; echo "$result" | jq -r '.aggregations.by_device.buckets[] | "    device \(.key): \(.doc_count)"' 2>/dev/null
echo "  by background:"; echo "$result" | jq -r '.aggregations.by_background.buckets[] | "    background=\(.key): \(.doc_count)"' 2>/dev/null
echo "  by channel:"; echo "$result" | jq -r '.aggregations.by_channel.buckets[] | "    channel \(.key): \(.doc_count)"' 2>/dev/null
read_first="$(echo "$result" | jq -r '.aggregations.first.hits.hits[0]._source["@timestamp"] // "n/a"' 2>/dev/null)"
read_last="$(echo "$result" | jq -r '.aggregations.last.hits.hits[0]._source["@timestamp"] // "n/a"' 2>/dev/null)"
echo "  time range: $read_first to $read_last"

section "5. PhysicalCellProgramLog BREAKDOWN (time window)"
result="$(es_query "$ES_URL/$idx/_search" -d "$(read_body PhysicalCellProgramLog)")"
write_total="$(echo "$result" | jq -r '.aggregations.total.value // 0' 2>/dev/null || echo "?")"
echo "PhysicalCellProgramLog total: $write_total"
echo "  by device:"; echo "$result" | jq -r '.aggregations.by_device.buckets[] | "    device \(.key): \(.doc_count)"' 2>/dev/null
echo "  by background:"; echo "$result" | jq -r '.aggregations.by_background.buckets[] | "    background=\(.key): \(.doc_count)"' 2>/dev/null
echo "  by channel:"; echo "$result" | jq -r '.aggregations.by_channel.buckets[] | "    channel \(.key): \(.doc_count)"' 2>/dev/null
write_first="$(echo "$result" | jq -r '.aggregations.first.hits.hits[0]._source["@timestamp"] // "n/a"' 2>/dev/null)"
write_last="$(echo "$result" | jq -r '.aggregations.last.hits.hits[0]._source["@timestamp"] // "n/a"' 2>/dev/null)"
echo "  time range: $write_first to $write_last"

section "6. PhysicalCellProgramCompatibleLog (time window)"
compat_body='{"size":0,"query":{"bool":{"filter":[{"range":{"@timestamp":{"gte":"'$FROM_DATE'","lte":"'$TO_DATE'"}}},{"term":{"type":"PhysicalCellProgramCompatibleLog"}}]}},"aggs":{"total":{"value_count":{"field":"@timestamp"}},"by_device":{"terms":{"field":"device_index","size":10}}}}'
result="$(es_query "$ES_URL/$idx/_search" -d "$compat_body")"
compat_total="$(echo "$result" | jq -r '.aggregations.total.value // 0' 2>/dev/null || echo "?")"
echo "PhysicalCellProgramCompatibleLog total: $compat_total"
echo "$result" | jq -r '.aggregations.by_device.buckets[] | "    device \(.key): \(.doc_count)"' 2>/dev/null

section "7. LogicalCellProgramLog BREAKDOWN (time window)"
logical_body='{"size":0,"query":{"bool":{"filter":[{"range":{"@timestamp":{"gte":"'$FROM_DATE'","lte":"'$TO_DATE'"}}},{"term":{"type":"LogicalCellProgramLog"}}]}},"aggs":{"total":{"value_count":{"field":"@timestamp"}},"by_device":{"terms":{"field":"device_index","size":10}}}}'
result="$(es_query "$ES_URL/$idx/_search" -d "$logical_body")"
logical_total="$(echo "$result" | jq -r '.aggregations.total.value // 0' 2>/dev/null || echo "?")"
echo "LogicalCellProgramLog total: $logical_total"
echo "$result" | jq -r '.aggregations.by_device.buckets[] | "    device \(.key): \(.doc_count)"' 2>/dev/null

section "8. PageCopyBackLog BREAKDOWN (time window)"
cb_body='{"size":0,"query":{"bool":{"filter":[{"range":{"@timestamp":{"gte":"'$FROM_DATE'","lte":"'$TO_DATE'"}}},{"term":{"type":"PageCopyBackLog"}}]}},"aggs":{"total":{"value_count":{"field":"@timestamp"}},"by_device":{"terms":{"field":"device_index","size":10}},"by_background":{"terms":{"field":"background","size":5}}}}'
result="$(es_query "$ES_URL/$idx/_search" -d "$cb_body")"
copyback_total="$(echo "$result" | jq -r '.aggregations.total.value // 0' 2>/dev/null || echo "?")"
echo "PageCopyBackLog total: $copyback_total"
echo "  by device:"; echo "$result" | jq -r '.aggregations.by_device.buckets[] | "    device \(.key): \(.doc_count)"' 2>/dev/null
echo "  by background:"; echo "$result" | jq -r '.aggregations.by_background.buckets[] | "    background=\(.key): \(.doc_count)"' 2>/dev/null

section "9. GarbageCollectionLog BREAKDOWN (time window)"
gc_body='{"size":0,"query":{"bool":{"filter":[{"range":{"@timestamp":{"gte":"'$FROM_DATE'","lte":"'$TO_DATE'"}}},{"term":{"type":"GarbageCollectionLog"}}]}},"aggs":{"total":{"value_count":{"field":"@timestamp"}},"by_background":{"terms":{"field":"background","size":5}},"first":{"top_hits":{"size":1,"sort":[{"@timestamp":{"order":"asc"}}],"_source":{"includes":["@timestamp","device_index","background"]}}},"last":{"top_hits":{"size":1,"sort":[{"@timestamp":{"order":"desc"}}],"_source":{"includes":["@timestamp","device_index","background"]}}}}}'
result="$(es_query "$ES_URL/$idx/_search" -d "$gc_body")"
gc_total="$(echo "$result" | jq -r '.aggregations.total.value // 0' 2>/dev/null || echo "?")"
gc_bg="$(echo "$result" | jq -r '.aggregations.by_background.buckets[] | select(.key == true or .key_as_string == "true") | .doc_count' 2>/dev/null || echo 0)"
gc_fg="$(echo "$result" | jq -r '.aggregations.by_background.buckets[] | select(.key == false or .key_as_string == "false") | .doc_count' 2>/dev/null || echo 0)"
echo "GarbageCollectionLog total: $gc_total"
echo "  background GC: $gc_bg"
echo "  foreground GC: $gc_fg"
gc_first="$(echo "$result" | jq -r '.aggregations.first.hits.hits[0]._source["@timestamp"] // "n/a"' 2>/dev/null)"
gc_last="$(echo "$result" | jq -r '.aggregations.last.hits.hits[0]._source["@timestamp"] // "n/a"' 2>/dev/null)"
echo "  time range: $gc_first to $gc_last"

section "10. GARBAGE COLLECTOR DETAILED STATS (time window)"
if [[ "$gc_total" =~ ^[0-9]+$ ]] && (( gc_total > 0 )); then
  if [[ "$gc_bg" =~ ^[0-9]+$ ]]; then
    gc_fg_detail=$((gc_total - gc_bg))
    (( gc_fg_detail < 0 )) && gc_fg_detail=0
  else
    gc_fg_detail=""
    gc_bg=""
  fi
  if [[ -n "${gc_bg:-}" ]]; then
    awk -v t="$gc_total" -v b="$gc_bg" 'BEGIN{printf "GC events: %d\n  background: %d (%.2f%%)\n  foreground: %d (%.2f%%)\n",t,b,b*100.0/t,t-b,(t-b)*100.0/t}'
  else
    echo "GC events: $gc_total"
  fi
  gc_deep_body='{"size":0,"query":{"bool":{"filter":[{"range":{"@timestamp":{"gte":"'$FROM_DATE'","lte":"'$TO_DATE'"}}},{"term":{"type":"GarbageCollectionLog"}}]}},"aggs":{"by_device":{"terms":{"field":"device_index","size":10},"aggs":{"bg":{"filter":{"term":{"background":true}}}}},"per_interval":{"date_histogram":{"field":"@timestamp","fixed_interval":"30m"},"aggs":{"fg":{"filter":{"term":{"background":false}}}}},"per_minute":{"date_histogram":{"field":"@timestamp","fixed_interval":"1m"}}}}'
  result="$(es_query "$ES_URL/$idx/_search" -d "$gc_deep_body")" || true
  echo "  by device:"
  echo "$result" | jq -r '.aggregations.by_device.buckets[] | "    device \(.key): total=\(.doc_count) background=\(.bg.doc_count) foreground=\(.doc_count - .bg.doc_count)"' 2>/dev/null
  echo "  per 30min interval (total / foreground):"
  echo "$result" | jq -r '.aggregations.per_interval.buckets[] | select(.doc_count > 0) | "    \(.key_as_string): \(.doc_count) (fg=\(.fg.doc_count))"' 2>/dev/null
  peak_line="$(echo "$result" | jq -r '.aggregations.per_minute.buckets | map(select(.doc_count > 0)) | sort_by(-.doc_count)[0] | [.key_as_string, (.doc_count|tostring)] | @tsv' 2>/dev/null || true)"
  if [[ -n "${peak_line:-}" ]]; then
    IFS=$'\t' read -r peak_ts peak_cnt <<<"$peak_line"
    echo "  busiest minute: $peak_cnt GCs at $peak_ts"
  fi
  gc_e1="$(ts_to_epoch "$gc_first")"
  gc_e2="$(ts_to_epoch "$gc_last")"
  gc_detailed_span="$(span_secs "$gc_total" "$gc_e1" "$gc_e2")"
  echo "  first GC: $gc_first"
  echo "  last GC:  $gc_last"
  echo "  active GC span: ${gc_detailed_span}s"
  if (( gc_detailed_span > 0 )); then
    awk -v t="$gc_total" -v d="$gc_detailed_span" 'BEGIN{printf "  GC rate over span: %.4f events/sec (avg interval %.2fs between GCs)\n",t/d,d/t}'
    if [[ -n "${gc_fg_detail:-}" ]] && (( gc_fg_detail > 0 )); then
      awk -v f="$gc_fg_detail" -v d="$gc_detailed_span" 'BEGIN{printf "  foreground GC rate over span: %.4f events/sec\n",f/d}'
    fi
  else
    echo "  GC rate over span: n/a (span unknown)"
  fi
else
  echo "No garbage collection events in the selected time window."
fi

section "11. BlockEraseLog BREAKDOWN (time window)"
erase_body='{"size":0,"query":{"bool":{"filter":[{"range":{"@timestamp":{"gte":"'$FROM_DATE'","lte":"'$TO_DATE'"}}},{"term":{"type":"BlockEraseLog"}}]}},"aggs":{"total":{"value_count":{"field":"@timestamp"}},"by_device":{"terms":{"field":"device_index","size":10}}}}'
result="$(es_query "$ES_URL/$idx/_search" -d "$erase_body")"
erase_total="$(echo "$result" | jq -r '.aggregations.total.value // 0' 2>/dev/null || echo "?")"
echo "BlockEraseLog total: $erase_total"
echo "  by device:"; echo "$result" | jq -r '.aggregations.by_device.buckets[] | "    device \(.key): \(.doc_count)"' 2>/dev/null

section "12. LOW-LEVEL OPERATION COUNTS (time window)"
for logtype in RegisterReadLog RegisterWriteLog ChannelSwitchToReadLog ChannelSwitchToWriteLog; do
  body="{\"size\":0,\"query\":{\"bool\":{\"filter\":[{\"range\":{\"@timestamp\":{\"gte\":\"$FROM_DATE\",\"lte\":\"$TO_DATE\"}}},{\"term\":{\"type\":\"$logtype\"}}]}},\"aggs\":{\"total\":{\"value_count\":{\"field\":\"@timestamp\"}},\"by_device\":{\"terms\":{\"field\":\"device_index\",\"size\":10}}}}"
  result="$(es_query "$ES_URL/$idx/_search" -d "$body")"
  lt_total="$(echo "$result" | jq -r '.aggregations.total.value // 0' 2>/dev/null || echo "?")"
  echo "$logtype total: $lt_total"
  echo "  by device:"; echo "$result" | jq -r '.aggregations.by_device.buckets[] | "    device \(.key): \(.doc_count)"' 2>/dev/null
done

section "13. UTILIZATION STATS (time window)"
util_body='{"size":0,"query":{"bool":{"filter":[{"range":{"@timestamp":{"gte":"'$FROM_DATE'","lte":"'$TO_DATE'"}}},{"term":{"type":"SsdUtilizationLog"}}]}},"aggs":{"util_samples":{"value_count":{"field":"@timestamp"}},"avg_util":{"avg":{"field":"utilization_percent"}},"by_device":{"terms":{"field":"device_index","size":10},"aggs":{"samples":{"value_count":{"field":"@timestamp"}},"max_occupied":{"max":{"field":"occupied_pages"}},"total_pages":{"max":{"field":"total_pages"}}}},"last_by_device":{"terms":{"field":"device_index","size":10,"order":{"_key":"asc"}},"aggs":{"last":{"top_hits":{"size":1,"sort":[{"@timestamp":{"order":"desc"}}],"_source":{"includes":["utilization_percent","occupied_pages","total_pages","test.ssd.size","device_index"]}}}}},"max_occ":{"top_hits":{"size":1,"sort":[{"occupied_pages":{"order":"desc","unmapped_type":"long"}},{"@timestamp":{"order":"desc"}}],"_source":{"includes":["utilization_percent","total_pages","occupied_pages","test.ssd.size"]}}},"min_occ":{"top_hits":{"size":1,"sort":[{"occupied_pages":{"order":"asc","unmapped_type":"long"}},{"@timestamp":{"order":"desc"}}],"_source":{"includes":["utilization_percent","total_pages","occupied_pages","test.ssd.size"]}}}}}'
result="$(es_query "$ES_URL/$idx/_search" -d "$util_body")"
util_samples="$(echo "$result" | jq -r '.aggregations.util_samples.value // 0' 2>/dev/null || echo "?")"
echo "Utilization log samples: $util_samples"
echo "  per device:"; echo "$result" | jq -r '.aggregations.by_device.buckets[] | "    device \(.key): samples=\(.samples.value) max_occupied=\(.max_occupied.value // "?") total_pages=\(.total_pages.value // "?")"' 2>/dev/null
echo "  last known state per device:"; echo "$result" | jq -r '.aggregations.last_by_device.buckets[] | "    device \(.last.hits.hits[0]._source.device_index // .key): occupied=\(.last.hits.hits[0]._source.occupied_pages // "?") total=\(.last.hits.hits[0]._source.total_pages // "?") util=\(.last.hits.hits[0]._source.utilization_percent // "?") ssd_size=\(.last.hits.hits[0]._source["test.ssd.size"] // "?")"' 2>/dev/null

section "14. COMPUTED METRICS"

lat_file="$(mktemp)"
i=1
while [ "$i" -le "$SAMPLES" ]; do
  t_sec="$(curl -k -s -o /dev/null -w '%{time_total}' -u "elastic:$ELASTIC_PASSWORD" -H 'Content-Type: application/json' "$ES_URL/$idx/_search" -d '{"size":0,"query":{"range":{"@timestamp":{"gte":"'"$FROM_DATE"'","lte":"'"$TO_DATE"'"}}},"sort":[{"@timestamp":{"order":"desc"}}]}' || true)"
  ms="$(sec_to_ms "$t_sec")"
  echo "$ms" >>"$lat_file"
  i=$((i + 1))
done
n="$(wc -l <"$lat_file")"
if [[ "$n" -gt 0 ]]; then
  p95_idx=$(((95 * n + 99) / 100))
  (( p95_idx < 1 )) && p95_idx=1
  (( p95_idx > n )) && p95_idx="$n"
  p95="$(sort -n "$lat_file" | sed -n "${p95_idx}p")"
else
  p95="0"
fi
rm -f "$lat_file"

read_first_epoch="$(ts_to_epoch "$read_first")"
read_last_epoch="$(ts_to_epoch "$read_last")"
write_first_epoch="$(ts_to_epoch "$write_first")"
write_last_epoch="$(ts_to_epoch "$write_last")"
gc_first_epoch="$(ts_to_epoch "$gc_first")"
gc_last_epoch="$(ts_to_epoch "$gc_last")"

read_span_secs="$(span_secs "$read_total" "$read_first_epoch" "$read_last_epoch")"
write_span_secs="$(span_secs "$write_total" "$write_first_epoch" "$write_last_epoch")"
gc_span_secs="$(span_secs "$gc_total" "$gc_first_epoch" "$gc_last_epoch")"

read_iops="$(awk -v c="$read_total" -v d="$read_span_secs" 'BEGIN{if(d<=0)print 0;else printf "%.6f",c/d}')"
write_iops="$(awk -v c="$write_total" -v d="$write_span_secs" 'BEGIN{if(d<=0)print 0;else printf "%.6f",c/d}')"

if [[ "$logical_total" =~ ^[0-9]+$ && "$logical_total" -gt 0 ]]; then
  write_amplification="$(awk -v p="$write_total" -v l="$logical_total" 'BEGIN{printf "%.6f",p/l}')"
else
  write_amplification="0"
fi

foreground_gc_count=0
if [[ "$gc_bg" =~ ^[0-9]+$ && "$gc_total" =~ ^[0-9]+$ ]]; then
  foreground_gc_count=$((gc_total - gc_bg))
  (( foreground_gc_count < 0 )) && foreground_gc_count=0
fi
activity_span_secs="$(span_secs "$((read_total + write_total + logical_total + gc_total))" "$(ts_to_epoch "$first_ts")" "$(ts_to_epoch "$last_ts")")"
gc_rate="0"
foreground_gc_rate="0"
if [[ "$gc_total" =~ ^[0-9]+$ ]] && [[ "$activity_span_secs" -gt 0 ]]; then
  gc_rate="$(awk -v c="$gc_total" -v d="$activity_span_secs" 'BEGIN{printf "%.6f",c/d}')"
  foreground_gc_rate="$(awk -v c="$foreground_gc_count" -v d="$activity_span_secs" 'BEGIN{printf "%.6f",c/d}')"
fi

util_last_src="$(echo "$result" | jq -c '.aggregations.last_by_device.buckets[0].last.hits.hits[0]._source // {}' 2>/dev/null || echo "{}")"
util_max_src="$(echo "$result" | jq -c '.aggregations.max_occ.hits.hits[0]._source // {}' 2>/dev/null || echo "{}")"
util_min_src="$(echo "$result" | jq -c '.aggregations.min_occ.hits.hits[0]._source // {}' 2>/dev/null || echo "{}")"
util_avg_raw="$(echo "$result" | jq -r '.aggregations.avg_util.value // empty' 2>/dev/null || echo "")"

fmt_pct() { local u="$1"; [[ -z "${u:-}" ]] && { echo "UNKNOWN"; return; }; awk -v x="$u" 'BEGIN{printf "%.4f",x*100.0}'; }
util_from_src() {
  local src="$1" u tp op
  u="$(echo "$src" | jq -r '.utilization_percent // empty' 2>/dev/null)"
  tp="$(echo "$src" | jq -r '.total_pages // empty' 2>/dev/null)"
  op="$(echo "$src" | jq -r '.occupied_pages // empty' 2>/dev/null)"
  [[ -n "${u:-}" ]] && { awk -v x="$u" 'BEGIN{if(x>1.0)printf "%.12f",x/100.0;else printf "%.12f",x}'; return; }
  [[ -n "${tp:-}" && -n "${op:-}" ]] && { awk -v o="$op" -v t="$tp" 'BEGIN{if(t<=0)print "";else printf "%.12f",o/t}'; return; }
  echo ""
}

utilization_last="$(util_from_src "$util_last_src")"
utilization_max="$(util_from_src "$util_max_src")"
utilization_min="$(util_from_src "$util_min_src")"
utilization_avg=""
if [[ -n "${util_avg_raw:-}" ]]; then
  utilization_avg="$(awk -v x="$util_avg_raw" 'BEGIN{if(x>1.0)printf "%.12f",x/100.0;else printf "%.12f",x}')"
fi

total_pages="$(echo "$util_last_src" | jq -r '.total_pages // empty' 2>/dev/null)"
ssd_size_bytes="$(echo "$util_last_src" | jq -r '."test.ssd.size" // empty' 2>/dev/null)"
page_size_bytes=""
if [[ -n "${ssd_size_bytes:-}" && -n "${total_pages:-}" && "$total_pages" != "0" ]]; then
  page_size_bytes="$(awk -v s="$ssd_size_bytes" -v t="$total_pages" 'BEGIN{printf "%.12f",s/t}')"
fi

read_speed_mbps="-"
write_speed_mbps="-"
if [[ -n "${page_size_bytes:-}" ]]; then
  read_speed_mbps="$(awk -v c="$read_total" -v p="$page_size_bytes" -v d="$read_span_secs" 'BEGIN{if(d<=0)print 0;else printf "%.6f",(c*p*8.0)/(d*1000000.0)}')"
  write_speed_mbps="$(awk -v c="$write_total" -v p="$page_size_bytes" -v d="$write_span_secs" 'BEGIN{if(d<=0)print 0;else printf "%.6f",(c*p*8.0)/(d*1000000.0)}')"
fi

echo "Activity duration:        ${activity_span_secs}s"
echo ""
echo "Physical writes:          $write_total (background: $gc_bg) span=${write_span_secs}s"
echo "Physical reads:           $read_total span=${read_span_secs}s"
echo "Logical writes:           $logical_total"
echo "Garbage collections:      $gc_total (background: $gc_bg, foreground: $foreground_gc_count) span=${gc_span_secs}s"
echo ""
echo "P95 latency:              ${p95}ms"
echo "Read IOPS:                $read_iops"
echo "Write IOPS:               $write_iops"
echo "Read speed:               $read_speed_mbps Mb/s"
echo "Write speed:              $write_speed_mbps Mb/s"
echo "Write amplification:      $write_amplification"
echo "GC rate:                  $gc_rate events/sec (foreground: $foreground_gc_rate)"
echo ""
echo "Utilization samples:      $util_samples"
echo "Utilization last:         ${utilization_last:-UNKNOWN} ($(fmt_pct "${utilization_last:-}")%)"
echo "Utilization avg:          ${utilization_avg:-UNKNOWN} ($(fmt_pct "${utilization_avg:-}")%)"
echo "Utilization max:          ${utilization_max:-UNKNOWN} ($(fmt_pct "${utilization_max:-}")%)"
echo "Utilization min:          ${utilization_min:-UNKNOWN} ($(fmt_pct "${utilization_min:-}")%)"
echo "Page size:                ${page_size_bytes:-UNKNOWN} bytes"
echo "======================================================================="
echo ""

echo "# END OF STATS"
} > "$tmp_out"

case "$MODE" in
  print)
    cat "$tmp_out"
    rm -f "$tmp_out"
    ;;
  file)
    mv "$tmp_out" "$OUTPUT" ;;
  both)
    cat "$tmp_out"
    mv "$tmp_out" "$OUTPUT" ;;
esac

echo ""
if [[ "$MODE" == "print" ]]; then
  echo "$PREFIX Printed stats to stdout (use -m file or -m both to save to a file)"
else
  echo "$PREFIX Stats saved to: $OUTPUT"
fi
