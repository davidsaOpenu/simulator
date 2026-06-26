#!/bin/bash
# =============================================================================
# docker-test-host-elk.sh  (renamed from docker-test-host.sh)
#
# Runs each host simulation test-case GROUP under its own unique RUN_ID, waits
# for ELK ingestion to settle (count stops growing), then asserts the nine
# performance metrics fall within tight per-case bounds. Replaces the flaky
# time-window approach of elk_performance_test.sh (see
# eVSSIM/docs/HOST_TESTS_CASES_METRICS.md). Requires the ELK stack already
# running (run-ci.sh starts it before invoking this script), host curl/jq, and
# ELASTIC_PASSWORD.
#
# Modes:
#   RUNS=1 (default, CI gating): one run per group; assert each metric against
#     the characterized bounds in host_tests_bounds.env. Fast; does NOT rewrite
#     the committed metrics doc.
#   RUNS=N>1 (characterization): N runs per group, with the group ORDER permuted
#     on every round (rotated), to prove the per-run.id metrics are independent
#     of run order. Writes the full run #1..#N matrix to
#     eVSSIM/docs/HOST_TESTS_CASES_METRICS.md (bounds become report-only). Derive
#     bounds from that doc with derive_bounds.py. Typical:
#       SKIP_LEAKCHECK=1 RUNS=5 ./docker-test-host-elk.sh
#       python3 ../ELK/derive_bounds.py
#
# Gated metrics: logical write count (exact), write count, write amplification,
# GC invocations, disk utilization (tolerant). IOPS / speed are report-only.
# =============================================================================
set -Eeo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
source ./builder.sh                          # evssim_run_at_folder, env, etc.

ELK_DIR="$EVSSIM_ROOT_PATH/simulator/infra/ELK"
source "$ELK_DIR/elk_run_metrics.sh"          # elk_settle, elk_query_metrics

SIM_DIR="$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/simulation"
UNIT_DIR="$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/unit"
METRICS_MD="$EVSSIM_ROOT_PATH/simulator/eVSSIM/docs/HOST_TESTS_CASES_METRICS.md"
BOUNDS_ENV="$ELK_DIR/host_tests_bounds.env"

RUNS="${RUNS:-1}"                             # 1 = CI gating; >1 = characterization

# Per-group runs are native (see leak-check note below); valgrind is used only
# for the full --ci leak-check pass.
VALGRIND_CMD="${VALGRIND_CMD:-valgrind --leak-check=full --error-exitcode=2}"

# Load characterized per-case bounds for gating (RUNS=1). During characterization
# we are *deriving* bounds, so we don't gate on the old ones (report-only).
if (( RUNS == 1 )) && [[ -f "$BOUNDS_ENV" ]]; then
  set -o allexport; source "$BOUNDS_ENV"; set +o allexport
fi

if [[ -f "$ELK_DIR/.env" && -z "${ELASTIC_PASSWORD:-}" ]]; then
  set -o allexport; source "$ELK_DIR/.env"; set +o allexport
fi
: "${ELASTIC_PASSWORD:?ELASTIC_PASSWORD must be set or present in infra/ELK/.env}"
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY

# name : gtest-flag
# Gate the TRAFFIC-BEHAVIOUR tests: they run representative traffic, so their
# per-run.id metrics are a meaningful regression signal. Two suites are excluded
# on PURPOSE grounds — their job is a specific feature, not "metrics for known
# traffic", so a metric failure there would tell us nothing (their own gtest
# assertions already cover them, and they still run in the --ci leak pass below):
#   log_mgr_tests        - exercises the standalone logger mechanics; not wired to
#                          the ELK pipeline, so it emits ZERO ES documents anyway
#   offline_logger_tests - tests the log AUTO-DELETE feature, so it deletes its own
#                          logs mid-run; bounding a self-truncating stream is noise
CASES=(
  "sector_tests:--sector-tests"
  "object_tests:--object-tests"
  "ssd_io_emulator_tests:--ssd-io-emulator-tests"
  "multi_device_tests:--multi_device_tests"
  "ssd_program_compatible_tests:--ssd_program_compatible_test"
)
# Heavy groups (~6-20M events/run). Same purpose + same settle+bounds path, but
# shipping that many docs per run is slow and disk-heavy, so they are opt-in via
# RUN_HEAVY=1 and need a larger SETTLE_TIMEOUT_SECS.
if [[ "${RUN_HEAVY:-}" == "1" ]]; then
  CASES+=(
    "ssd_write_read_tests:--ssd_write_read_test"
    "onfi_ops_tests:--onfi_ops_test"
  )
fi
if [[ -n "${METRIC_GROUPS:-}" ]]; then
  read -ra CASES <<< "$METRIC_GROUPS"
fi
# Asserted (gated) metrics, by tier:
#   logical_write_count        - EXACT (pipeline sentinel: = the traffic; any
#                                deviation means events were dropped/added)
#   write_count, write_amplification, gc_invocations, disk_utilization_avg
#                              - TOLERANT (real FTL/GC-timing variance; bounds catch
#                                gross model regressions)
# read_iops/write_iops/read_speed/write_speed and read_count are REPORT-ONLY: they
# are synthetic ratios over modeled time (no wall clock post-pivot), so a failure
# isn't interpretable. They are emitted to the doc but not gated.
METRICS=(logical_write_count write_count write_amplification gc_invocations disk_utilization_avg)

uuid() { cat /proc/sys/kernel/random/uuid; }
fail=0

lt() { awk -v a="$1" -v b="$2" 'BEGIN{ exit !(a+0 < b+0) }'; }
gt() { awk -v a="$1" -v b="$2" 'BEGIN{ exit !(a+0 > b+0) }'; }

# Scratch dir holding per-(group,run) metric blocks for the doc assembled at the end.
RESULTS_DIR="$(mktemp -d)"
trap 'rm -rf "$RESULTS_DIR"; [[ -n "${LEAK_PID:-}" ]] && kill "$LEAK_PID" 2>/dev/null || true' EXIT

# Leak coverage: one full --ci run per binary (all suites run => all params freed),
# as run_all_host_tests.sh did. It runs in the BACKGROUND, overlapped with the
# metric runs below: its events carry no EVSSIM_RUN_ID, so they cannot pollute the
# run.id-scoped metrics, and overlapping hides the whole metric pass inside the
# much longer valgrind pass instead of adding it to the CI wall clock.
# (SKIP_LEAKCHECK=1 skips it for a quick characterization pass.)
unset EVSSIM_RUN_ID
LEAK_PID=""
if [[ "${SKIP_LEAKCHECK:-}" != "1" ]]; then
  LEAK_LOG="$RESULTS_DIR/leak_check.log"
  # The leak-check container runs concurrently with the metric-run containers, so it
  # gets fully private state: no published host ports (would clash), its own data dir
  # (ssd.conf + nand files are regenerated per test) and its own logs dir (its events
  # are untagged, so nothing downstream needs them shipped to ES).
  LEAK_TMP="$RESULTS_DIR/leak_isolation"
  mkdir -p "$LEAK_TMP/data/0" "$LEAK_TMP/data/1" "$LEAK_TMP/data/2" "$LEAK_TMP/logs"
  echo "[docker-test-host-elk] leak-check (valgrind, full --ci suite) started in background; its output is printed after the metric runs"
  (
    export EVSSIM_DOCKER_PORTS_OPTION=""
    export EVSSIM_DOCKER_XOPTIONS="${EVSSIM_DOCKER_XOPTIONS:-} -v $LEAK_TMP/data:$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/data -v $LEAK_TMP/logs:$EVSSIM_DOCKER_ROOT_PATH/logs"
    evssim_run_at_folder "$SIM_DIR" "$VALGRIND_CMD ./simulation_tests_main --ci"
    evssim_run_at_folder "$UNIT_DIR" "$VALGRIND_CMD ./unit_tests_main --ci"
  ) > "$LEAK_LOG" 2>&1 &
  LEAK_PID=$!
fi

# Run one group in the container, tagging every event with EVSSIM_RUN_ID.
run_group() {
  local flag="$1" run_id="$2" out
  export EVSSIM_RUN_ID="$run_id"
  out="$(evssim_run_at_folder "$SIM_DIR" "./simulation_tests_main $flag" 2>&1)" || {
    echo "[docker-test-host-elk] FAIL: group $flag exited non-zero (test failure)" >&2
    echo "$out" | tail -25 >&2
    return 1
  }
  return 0
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

process_group() {  # name flag run_number
  local name="$1" flag="$2" run_no="$3" metrics
  local run_id; run_id="$(uuid)"
  echo "================================================================"
  echo "[docker-test-host-elk] run #$run_no  group=$name  flag=$flag  run.id=$run_id"
  run_group "$flag" "$run_id" || { fail=1; return 1; }
  elk_settle "$run_id" || { fail=1; return 1; }
  metrics="$(elk_query_metrics "$run_id")"

  # Stash this run's metrics for the doc assembled after all rounds.
  echo "$metrics" > "$RESULTS_DIR/$name.run$run_no"

  local m val
  for m in "${METRICS[@]}"; do
    val="$(echo "$metrics" | sed -nE "s/^$m=(.*)/\1/p")"
    assert_metric "$name" "$m" "$val"
  done
}

# Case order for round r: rotate the CASES list left by (r-1) so each round runs
# the groups in a different order (round 1 = as-listed). This is what proves the
# metrics are order-independent: same per-case numbers regardless of position.
order_for_round() {  # round -> prints CASES indices in this round's order
  local r="$1" n="${#CASES[@]}" i
  for ((i = 0; i < n; i++)); do echo $(( (i + r - 1) % n )); done
}

declare -a ORDER_LOG=()
for ((r = 1; r <= RUNS; r++)); do
  order_names=""
  for idx in $(order_for_round "$r"); do
    entry="${CASES[$idx]}"
    process_group "${entry%%:*}" "${entry##*:}" "$r" || true
    order_names+="${entry%%:*} "
  done
  ORDER_LOG+=("run #$r order: ${order_names% }")
done

# Assemble the metrics doc only when characterizing (RUNS>1); CI gating (RUNS=1)
# must not churn the committed characterization artifact.
if (( RUNS > 1 )); then
  mkdir -p "$(dirname "$METRICS_MD")"
  {
    echo "# HOST_TESTS_CASES_METRICS (generated $(date -u +%Y-%m-%dT%H:%M:%SZ))"
    echo "# $RUNS runs per case; case order permuted each round (see below) to show"
    echo "# the per-run.id metrics do not depend on run order. Bounds in"
    echo "# host_tests_bounds.env are derived from this file by derive_bounds.py."
    echo "#"
    for line in "${ORDER_LOG[@]}"; do echo "# $line"; done
    # Blank line BEFORE each case, not after, so the file has no trailing blank line (lint).
    for entry in "${CASES[@]}"; do
      name="${entry%%:*}"
      echo ""
      echo "test case: $name"
      for ((r = 1; r <= RUNS; r++)); do
        echo "run #$r"
        cat "$RESULTS_DIR/$name.run$r" 2>/dev/null || echo "(run #$r failed — see CI log)"
      done
    done
  } > "$METRICS_MD"
  echo "[docker-test-host-elk] wrote $METRICS_MD ($RUNS runs/case, permuted order)"
fi

# Per-group runs above are native: they only emit run.id-scoped events for the
# metric assertions. A filtered subset orphans the other suites'
# INSTANTIATE_TEST_CASE_P params (freed only when their test runs), which
# valgrind flags as leaks — hence the full --ci background pass started above.
if [[ -n "$LEAK_PID" ]]; then
  leak_rc=0
  wait "$LEAK_PID" || leak_rc=$?
  LEAK_PID=""
  echo "================================================================"
  echo "[docker-test-host-elk] leak-check (valgrind, full --ci suite) output:"
  cat "$LEAK_LOG"
  if (( leak_rc )); then
    echo "[docker-test-host-elk] leak-check FAILED (exit $leak_rc)"
    fail=1
  fi
fi

if (( fail )); then
  echo "[docker-test-host-elk] RESULT: FAIL"; exit 1
fi
echo "[docker-test-host-elk] RESULT: PASS"
