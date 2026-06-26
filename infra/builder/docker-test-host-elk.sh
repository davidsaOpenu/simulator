#!/bin/bash
# =============================================================================
# docker-test-host-elk.sh  (renamed from docker-test-host.sh)
#
# Runs each host simulation test-case GROUP under its own unique RUN_ID, then
# reconciles ELK ingestion and asserts the nine performance metrics fall within
# tight per-case bounds. Replaces the flaky time-window approach of
# elk_performance_test.sh (see eVSSIM/docs/HOST_TESTS_CASES_METRICS.md and
# docs/adr/0002-0004). Requires the ELK stack already running (run-ci.sh starts
# it before invoking this script), host-side curl/jq, and ELASTIC_PASSWORD.
#
# Each group runs once, reconciles, and asserts its metrics against the bounds in
# host_tests_bounds.env (absent => report-only). The per-case bounds were derived
# offline by characterizing each group across repeated + permuted runs.
#
# The 9 metrics: write count, logical write count, read IOPS, write IOPS,
# write amplification, read speed, write speed, GC invocations, disk utilization.
# =============================================================================
set -Eeo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
source ./builder.sh                          # evssim_run_at_folder, env, etc.

ELK_DIR="$EVSSIM_ROOT_PATH/simulator/infra/ELK"
source "$ELK_DIR/elk_run_metrics.sh"          # elk_reconcile, elk_query_metrics

SIM_DIR="$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/simulation"
UNIT_DIR="$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/unit"
METRICS_MD="$EVSSIM_ROOT_PATH/simulator/eVSSIM/docs/HOST_TESTS_CASES_METRICS.md"
BOUNDS_ENV="$ELK_DIR/host_tests_bounds.env"

# Per-group runs are native (see leak-check note below); valgrind is used only
# for the full --ci leak-check pass.
VALGRIND_CMD="${VALGRIND_CMD:-valgrind --leak-check=full --error-exitcode=2}"

# Load characterized per-case bounds if present (BOUND_<GROUP>_<METRIC>_MIN/MAX).
# Absent => report-only (lets the pipeline run before bounds are derived).
[[ -f "$BOUNDS_ENV" ]] && { set -o allexport; source "$BOUNDS_ENV"; set +o allexport; }

if [[ -f "$ELK_DIR/.env" && -z "${ELASTIC_PASSWORD:-}" ]]; then
  set -o allexport; source "$ELK_DIR/.env"; set +o allexport
fi
: "${ELASTIC_PASSWORD:?ELASTIC_PASSWORD must be set or present in infra/ELK/.env}"
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY

# name : gtest-flag
# ELK-metric-gated groups: ONLY those whose events fully flow through the
# FTL -> offline-analyzer -> filebeat -> ES pipeline and reconcile exactly
# (manifest == ES), verified empirically. Excluded (still run for correctness +
# leaks in the full --ci pass below, just not metric-gated):
#   log_mgr_tests              - writes to a standalone logger; events never ship (ES=0)
#   ssd_write_read_test        - ~20M events/run; too heavy to reconcile per run
#   onfi_ops_test              - ~5.9M events/run; too heavy to reconcile per run
#   ssd_program_compatible_test- only partially ships (manifest >> ES)
#   offline_logger_tests       - flipAuto() deliberately DELETES log files mid-test (it
#                                tests the auto-delete feature), so emitted events never
#                                all reach ES -> un-reconcilable by design, not a defect
CASES=(
  "sector_tests:--sector-tests"
  "object_tests:--object-tests"
  "ssd_io_emulator_tests:--ssd-io-emulator-tests"
  "multi_device_tests:--multi_device_tests"
)
# Metrics asserted against bounds (subset of emitted keys).
METRICS=(write_count logical_write_count read_iops write_iops write_amplification
         read_speed_mbps write_speed_mbps gc_invocations disk_utilization_avg)

uuid() { cat /proc/sys/kernel/random/uuid; }
fail=0

lt() { awk -v a="$1" -v b="$2" 'BEGIN{ exit !(a+0 < b+0) }'; }
gt() { awk -v a="$1" -v b="$2" 'BEGIN{ exit !(a+0 > b+0) }'; }

# Run one group in the container under RUN_ID; echo the emitted manifest count.
run_group() {
  local flag="$1" run_id="$2" out manifest
  export EVSSIM_RUN_ID="$run_id"
  out="$(evssim_run_at_folder "$SIM_DIR" "./simulation_tests_main $flag" 2>&1)" || {
    echo "[docker-test-host-elk] FAIL: group $flag exited non-zero (test failure)" >&2
    echo "$out" | tail -25 >&2
    return 1
  }
  manifest="$(echo "$out" | sed -nE 's/.*EVSSIM_RUN_MANIFEST run_id=[^ ]* events=([0-9]+).*/\1/p' | tail -1)"
  [[ -n "$manifest" ]] || { echo "[docker-test-host-elk] FAIL: no manifest from $flag" >&2; return 1; }
  echo "$manifest"
}

assert_metric() {  # group metric value
  local g="$1" m="$2" v="$3"
  local lo="BOUND_${g^^}_${m^^}_MIN" hi="BOUND_${g^^}_${m^^}_MAX"
  lo="${!lo:-}"; hi="${!hi:-}"
  if [[ -z "$lo" && -z "$hi" ]]; then
    printf '    %-24s = %-14s (no bound — report only)\n' "$m" "$v"; return
  fi
  local ok=1
  [[ -n "$lo" ]] && lt "$v" "$lo" && ok=0
  [[ -n "$hi" ]] && gt "$v" "$hi" && ok=0
  if (( ok )); then
    printf '    OK   %-24s = %-14s [%s, %s]\n' "$m" "$v" "${lo:- }" "${hi:- }"
  else
    printf '    FAIL %-24s = %-14s [%s, %s]\n' "$m" "$v" "${lo:- }" "${hi:- }"; fail=1
  fi
}

process_group() {  # name flag run_label
  local name="$1" flag="$2" label="$3" manifest metrics
  # Generate the run id HERE (parent scope). run_group runs in a $(...) subshell,
  # so its `export EVSSIM_RUN_ID` would not survive back to us — reconcile must use
  # the same id the container tagged events with, not an empty parent var.
  local run_id; run_id="$(uuid)"
  echo "================================================================"
  echo "[docker-test-host-elk] $label  group=$name  flag=$flag"
  manifest="$(run_group "$flag" "$run_id")" || { fail=1; return 1; }
  echo "[docker-test-host-elk] manifest events=$manifest run.id=$run_id"
  elk_reconcile "$run_id" "$manifest" || { fail=1; return 1; }
  metrics="$(elk_query_metrics "$run_id")"

  {
    echo "test case: $name"
    echo "$label"
    echo "$metrics"
    echo ""
  } >> "$METRICS_MD"

  local m val
  for m in "${METRICS[@]}"; do
    val="$(echo "$metrics" | sed -nE "s/^$m=(.*)/\1/p")"
    assert_metric "$name" "$m" "$val"
  done
}

mkdir -p "$(dirname "$METRICS_MD")"
echo "# HOST_TESTS_CASES_METRICS (generated $(date -u +%Y-%m-%dT%H:%M:%SZ))" > "$METRICS_MD"

for entry in "${CASES[@]}"; do
  process_group "${entry%%:*}" "${entry##*:}" "run #1" || true
done

# Per-group runs above are native: they only emit run.id-scoped events for the
# metric assertions. A filtered subset orphans the other suites'
# INSTANTIATE_TEST_CASE_P params (freed only when their test runs), which
# valgrind flags as leaks. Leak coverage is preserved by one full --ci run per
# binary (all suites run => all params freed), as run_all_host_tests.sh did.
echo "[docker-test-host-elk] leak-check (valgrind, full --ci suite)"
evssim_run_at_folder "$SIM_DIR" "$VALGRIND_CMD ./simulation_tests_main --ci"
evssim_run_at_folder "$UNIT_DIR" "$VALGRIND_CMD ./unit_tests_main --ci"

if (( fail )); then
  echo "[docker-test-host-elk] RESULT: FAIL"; exit 1
fi
echo "[docker-test-host-elk] RESULT: PASS"
