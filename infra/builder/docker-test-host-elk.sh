#!/bin/bash
set -Eeo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
source ./builder.sh

ELK_DIR="$EVSSIM_ROOT_PATH/simulator/infra/ELK"
source "$ELK_DIR/elk_run_metrics.sh"

SIM_DIR="$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/simulation"
UNIT_DIR="$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/unit"
METRICS_MD="$EVSSIM_ROOT_PATH/simulator/eVSSIM/docs/HOST_TESTS_CASES_METRICS.md"
BOUNDS_ENV="$ELK_DIR/host_tests_bounds.env"

RUNS="${RUNS:-1}"

VALGRIND_CMD="${VALGRIND_CMD:-valgrind --leak-check=full --error-exitcode=2}"

if (( RUNS == 1 )) && [[ -f "$BOUNDS_ENV" ]]; then
  set -o allexport; source "$BOUNDS_ENV"; set +o allexport
fi

if [[ -f "$ELK_DIR/.env" && -z "${ELASTIC_PASSWORD:-}" ]]; then
  set -o allexport; source "$ELK_DIR/.env"; set +o allexport
fi
: "${ELASTIC_PASSWORD:?ELASTIC_PASSWORD must be set or present in infra/ELK/.env}"
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY

CASES=(
  "sector_tests:--sector-tests"
  "object_tests:--object-tests"
  "ssd_io_emulator_tests:--ssd-io-emulator-tests"
  "multi_device_tests:--multi_device_tests"
  "ssd_program_compatible_tests:--ssd_program_compatible_test"
)
if [[ "${RUN_HEAVY:-}" == "1" ]]; then
  CASES+=(
    "ssd_write_read_tests:--ssd_write_read_test"
    "onfi_ops_tests:--onfi_ops_test"
  )
fi
if [[ -n "${METRIC_GROUPS:-}" ]]; then
  read -ra CASES <<< "$METRIC_GROUPS"
fi

fail=0

RESULTS_DIR="$(mktemp -d)"
trap 'rm -rf "$RESULTS_DIR"; [[ -n "${LEAK_PID:-}" ]] && kill "$LEAK_PID" 2>/dev/null || true' EXIT

LEAK_PID=""
if [[ "${SKIP_LEAKCHECK:-}" != "1" ]]; then
  LEAK_LOG="$RESULTS_DIR/leak_check.log"
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

run_group() {
  local flag="$1" out
  out="$(evssim_run_at_folder "$SIM_DIR" "./simulation_tests_main $flag" 2>&1)" || {
    echo "[docker-test-host-elk] FAIL: group $flag exited non-zero (test failure)" >&2
    echo "$out" | tail -25 >&2
    return 1
  }
  return 0
}

process_group() {
  local name="$1" flag="$2" run_no="$3" metrics
  echo "================================================================"
  echo "[docker-test-host-elk] run #$run_no  group=$name  flag=$flag"
  elk_wipe || { fail=1; return 1; }
  run_group "$flag" || { fail=1; return 1; }
  elk_settle || { fail=1; return 1; }
  metrics="$(elk_query_metrics)"

  echo "$metrics" > "$RESULTS_DIR/$name.run$run_no"

  elk_assert_case "$name" "$metrics" || fail=1
}

order_for_round() {
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

if (( RUNS > 1 )); then
  mkdir -p "$(dirname "$METRICS_MD")"
  {
    echo "# HOST_TESTS_CASES_METRICS (generated $(date -u +%Y-%m-%dT%H:%M:%SZ))"
    echo "# $RUNS runs per case; case order permuted each round (see below) to show"
    echo "# the metrics do not depend on run order. Bounds in"
    echo "# host_tests_bounds.env are derived from this file by derive_bounds.py."
    echo "#"
    for line in "${ORDER_LOG[@]}"; do echo "# $line"; done
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
