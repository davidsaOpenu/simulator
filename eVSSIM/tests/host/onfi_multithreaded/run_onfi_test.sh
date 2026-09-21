#!/bin/bash

set -uo pipefail

ONFI_TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM_PATH="$(cd "$ONFI_TEST_DIR/../../../.." && pwd)"
LOGS_DIR="$(cd "$SIM_PATH/../logs" && pwd)"
ELK_DIR="$SIM_PATH/infra/ELK"

cd "$SIM_PATH/infra/builder"
source env.sh > /dev/null
source builder.sh
source $ELK_DIR/.env
cd "$ONFI_TEST_DIR"

ELK_INSTALL="$ELK_DIR/install_and_start_elk.sh"
ELK_CLEAN="$ELK_DIR/elk_cleanup.sh"
ELK_STATS_COLLECT_SCRIPT="$ELK_DIR/elk_stats_collect.sh"
ES_URL="https://localhost:9200"
TEST_BINARY="./onfi_multithreaded_main"

MODE=""
BUILD=0
COLLECT_ELK=1
LOOKBACK_HOURS=24
ELK_WAIT_TIME=60
ELK_STARTED=0
VERSION="ubuntu-14.04"
OUT_DIR="$ONFI_TEST_DIR/onfi_results"

PAGES="64"
BLOCKS="4096"
FLASHES="8"
OPS="1000000"


usage() {
    cat <<'EOF'
Usage: ./run_onfi_test.sh [options]

Options:
  --mode serial|mt        dispatch mode to run       (required)
  --ops N                 total operations           (default: 1000000)
  --pages N               pages per block            (default: 64)
  --blocks N              blocks per flash           (default: 4096)
  --flashes N             number of flash chips      (default: 8)
  --lookback N            ELK lookback hours         (default: 24)
  --elk-wait SEC          max seconds to wait for
                          full ELK ingestion before
                          collecting stats           (default: 600)
  --version S             operational system version (default: ubuntu-14.04)
  --out-dir DIR           output directory for
                          run logs                   (default: onfi_results)
  --build                 force a rebuild first
  --no-elk                skip ELK stats collection
  -h, --help              show this help

Environment:
  ELASTIC_PASSWORD        for ELK collection (or infra/ELK/.env)
EOF

    exit 0
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --mode)      MODE="$2"; shift 2 ;;
            --pages)     PAGES="$2"; shift 2 ;;
            --blocks)    BLOCKS="$2"; shift 2 ;;
            --flashes)   FLASHES="$2"; shift 2 ;;
            --ops)       OPS="$2"; shift 2 ;;
            --lookback)  LOOKBACK_HOURS="$2"; shift 2 ;;
            --elk-wait)  ELK_WAIT_TIME="$2"; shift 2 ;;
            --version)   VERSION="$2"; shift 2 ;;
            --out-dir)   OUT_DIR="$2"; shift 2 ;;
            --build)     BUILD=1; shift ;;
            --no-elk)    COLLECT_ELK=0; shift ;;
            -h|--help)   usage ;;
            *) echo "Unknown option: $1" >&2; usage ;;
        esac
    done

    # Reject missing or unknown modes before doing any work.
    case "$MODE" in
        serial|mt) ;;
        *) echo "ERROR: --mode is required and must be serial or mt" >&2; exit 1 ;;
    esac

    evssim_validate_version_arguments "$0" "${VERSION:-}" 1

    if [[ "$MODE" == "serial" ]]; then
        MULTITHREADED_FLAG=0
    else
        MULTITHREADED_FLAG=1
    fi
}

# Write a run-level header so each log file is self-describing and the
# serial vs multithreaded runs can be compared side by side.
write_log_header() {
    local log_file="$1"
    local total_pages=$((PAGES * BLOCKS * FLASHES))

    {
        echo "================================================================================"
        echo "# ONFI MULTITHREADED TEST RUN LOG"
        echo "================================================================================"
        echo "# Generated:    $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "# Mode:         $MODE"
        echo "# Version:      $VERSION"
        echo "# Command:      $(get_test_command)"
        if [[ "$COLLECT_ELK" -eq 1 ]]; then
            echo "# ELK:          enabled (lookback ${LOOKBACK_HOURS}h)"
        else
            echo "# ELK:          disabled"
        fi
        echo ""
        echo "================================================================================"
        echo "[test parameters]"
        echo "  pages per block:    $PAGES"
        echo "  blocks per flash:   $BLOCKS"
        echo "  flashes:            $FLASHES"
        echo "  total pages:        $total_pages"
        echo "  ops:                $OPS"
        echo "================================================================================"
        echo ""
    } >"$log_file"
}

# Append wall-clock timing and derived throughput after a run.
append_timing() {
    local log_file="$1" ms="$2"

    {
        echo ""
        echo "================================================================================"
        echo "[wall-clock timing]"
        echo "  total test wall:    ${ms} ms ($(awk -v t="$ms" 'BEGIN{printf "%.3f", t/1000}') s)"
        echo "  throughput:         $(awk -v o="$OPS" -v t="$ms" 'BEGIN{if(t<=0) printf "n/a"; else printf "%.1f ops/sec", o/(t/1000)}')"
        echo "  per-operation:      $(awk -v t="$ms" -v o="$OPS" 'BEGIN{if(o<=0) printf "n/a"; else printf "%.4f ms/op", t/o}')"
        echo "================================================================================"
    } >>"$log_file"
}

# True when Elasticsearch answers (auth errors still count as "reachable").
elk_is_running() {
    curl -k -s --connect-timeout 3 -o /dev/null "$ES_URL" 2>/dev/null
}

# Start ELK only when collection is enabled and the stack is not already up.
start_elk_if_needed() {
    [[ "$COLLECT_ELK" -eq 1 ]] || return 0

    if elk_is_running; then
        echo "[run_onfi_test] ELK already running; reusing it (leaving it up)"
        return 0
    fi

    if [[ ! -x "$ELK_INSTALL" ]]; then
        echo "[run_onfi_test] WARN: $ELK_INSTALL not found; skipping ELK startup" >&2
        return 0
    fi

    echo "[run_onfi_test] starting ELK ..."
    # Mark as started before the call so a partial startup is still cleaned up.
    ELK_STARTED=1
    mkdir -p "$LOGS_DIR"
    "$ELK_INSTALL" "$LOGS_DIR" "$ELK_DIR" || {
        echo "[run_onfi_test] WARN: ELK startup failed" >&2
        return 1
    }
}

# Remove the ELK stack, but only if this run started it, so a
# pre-existing stack is never disturbed.
elk_stop() {
    [[ "$ELK_STARTED" -eq 1 ]] || return 0
    echo "[run_onfi_test] stopping ELK (started by this run) ..."
    "$ELK_CLEAN" --complete-cleanup || true
}

elk_clear_all_logs() {
    echo "[run_onfi_test] delete old log files ..."
    rm -f $LOGS_DIR/*

    echo "[run_onfi_test] clean all logs from ELK ..."
    curl -k -s -u elastic:$ELASTIC_PASSWORD -X DELETE "${ES_URL}/_data_stream/filebeat-*"
    echo
    local rc="$?"
    if [[ "$rc" != 0 ]]; then
        echo "ELK cleanup failed with exit $rc"
        exit 1
    fi

    echo "[run_onfi_test] ELK cleaned successfully"
}

elk_collect_logs() {
    local output_file="$1"

    if [[ "$COLLECT_ELK" == 0 ]]; then
        echo "[run_onfi_test] skipping ELK log collection" | tee -a "$output_file"
        return 0
    fi

    if [[ ! -x "$ELK_STATS_COLLECT_SCRIPT" ]]; then
        echo "[run_onfi_test] can't find $ELK_STATS_COLLECT_SCRIPT" | tee -a "$output_file"
        return 1
    fi

    {
        echo ""
        echo "========================================================================"
        echo "[ELK stats]"
        echo "========================================================================"

        echo "[run_onfi_test] wait $ELK_WAIT_TIME seconds for all the logs to load into ELK ..."
        sleep $ELK_WAIT_TIME

        echo "[run_onfi_test] collecting ELK stats ..."
        if "$ELK_STATS_COLLECT_SCRIPT" -s "$SIM_PATH" -l "$LOOKBACK_HOURS"; then
            echo "[run_onfi_test] collected ELK stats successfully"
        else
            echo "[run_onfi_test] error while running ELK stats collection (stack unreachable or missing .env)"
            return 2
        fi
    } 2>&1 | tee -a "$output_file"
    return "${PIPESTATUS[0]}"
}

build_onfi_test() {
    pushd $SIM_PATH/infra/builder > /dev/null
    ./compile-host-tests.sh $VERSION
    local rc="$?"
    popd > /dev/null
    return $rc
}

get_test_command() {
    echo "$TEST_BINARY --onfi-multithreaded $MULTITHREADED_FLAG ${ARGS[@]+${ARGS[@]}}"
}

run_test_in_evssim() {
    evssim_run_at_folder "$VERSION" "simulator/eVSSIM/tests/host/onfi_multithreaded" "$(get_test_command)"
}


parse_args $@
trap "elk_stop" EXIT

echo; echo "============================================================================"
echo "[run_onfi_test] start (mode=$MODE)"

# Build test if needed
if [[ "$BUILD" == 1 || ! -x "$TEST_BINARY" ]]; then
    echo; echo "============================================================================"
    echo "[run_onfi_test] building $TEST_BINARY"
    echo
    build_onfi_test || { echo "ERROR: build failed" >&2; exit 1; }
fi

# Prepare test's arguments, only if overrode
ARGS=()
[[ -n "$PAGES" ]]   && ARGS+=(--pages "$PAGES")
[[ -n "$BLOCKS" ]]  && ARGS+=(--blocks "$BLOCKS")
[[ -n "$FLASHES" ]] && ARGS+=(--flashes "$FLASHES")
[[ -n "$OPS" ]]     && ARGS+=(--ops "$OPS")

mkdir -p "$OUT_DIR"
STAMP="$(date -u +%Y%m%d_%H%M%S)"
log_file="$OUT_DIR/onfi_test_${STAMP}_${MODE}.txt"

write_log_header "$log_file"
start_elk_if_needed
elk_clear_all_logs

# Run onfi test, capturing the test's output into the log file
echo; echo "============================================================================"
echo "[run_onfi_test] start test"
echo "$(get_test_command)"
echo
t0="$(date +%s%N)"
    run_test_in_evssim 2>&1 | tee -a "$log_file"
    rc="${PIPESTATUS[0]}"
t1="$(date +%s%N)"

if [[ "$rc" != "0" ]]; then
    echo "$TEST_BINARY FAILED with exit $rc"
    exit 1
fi

TEST_RUNNING_TIME=$(( (t1 - t0) / 1000000 ))
append_timing "$log_file" "$TEST_RUNNING_TIME"

echo; echo "============================================================================"
elk_collect_logs "$log_file"

# Print test summary
echo "
========================================================================
[run summary]
========================================================================
[run_onfi_test] onfi test finished successfully (mode=$MODE, wall-clock=${TEST_RUNNING_TIME}ms)
[run_onfi_test] results can be found in $log_file
[run_onfi_test] done
" | tee -a "$log_file"
