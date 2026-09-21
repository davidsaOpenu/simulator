#!/bin/bash

set -uo pipefail

ONFI_TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
SIMULATOR_DIR="$(realpath -m "$ONFI_TEST_DIR/../../../..")"
LOGS_DIR="$(realpath -m "$SIMULATOR_DIR/../logs")"
ELK_DIR="$(realpath -m "$SIMULATOR_DIR/infra/ELK")"
BUILDER_DIR="$(realpath -m "$SIMULATOR_DIR/infra/builder")"

cd "$BUILDER_DIR"
source env.sh 2>&1 > /dev/null
source builder.sh 2>&1 > /dev/null
cd "$ONFI_TEST_DIR"

ES_URL="https://localhost:9200"
ELK_INSTALL="$ELK_DIR/install_and_start_elk.sh"
ELK_CLEAN="$ELK_DIR/elk_cleanup.sh"
ELK_WIPE="python3 $ELK_DIR/host_metrics.py wipe"
ELK_READ="python3 $ELK_DIR/host_metrics.py read --extended"
TEST_OUTPUT_FILE="$ONFI_TEST_DIR/.onfi_test_output.txt"
ELK_READ_OUTPUT_FILE="$ONFI_TEST_DIR/.onfi_test_elk_read_stats.txt"
ONFI_TEST_BINARY="./onfi_multithreaded_main"

MODE=""
PAGES="64"
BLOCKS="4096"
FLASHES="8"
OPS="100000"

VERSION="ubuntu-26.04"
OUT_DIR="$ONFI_TEST_DIR/onfi_results"
TEST_ID=""
NEED_BUILD=0
NEED_ELK_MANAGE=0
NEED_ELK_WIPE=0
RESULTS_FILE=""
START_TIME=""
END_TIME=""

usage() {
    echo "Usage: ./run_onfi_test.sh [options]

Options:
  --mode serial|mt        dispatch mode to run       (required)
  --ops N                 total operations           (default: $OPS)
  --pages N               pages per block            (default: $PAGES)
  --blocks N              blocks per flash           (default: $BLOCKS)
  --flashes N             number of flash chips      (default: $FLASHES)
  --id N                  test id for logging        (default: none)
  --version S             operational system version (default: $VERSION)
  --out-dir DIR           output directory for
                          run logs                   (default: onfi_results)
  --elk-wipe              wipe logs from elk
  --build                 force a rebuild first
  -h, --help              show this help
"
    exit 0
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --mode)          MODE="$2"; shift 2 ;;
            --pages)         PAGES="$2"; shift 2 ;;
            --blocks)        BLOCKS="$2"; shift 2 ;;
            --flashes)       FLASHES="$2"; shift 2 ;;
            --ops)           OPS="$2"; shift 2 ;;
            --version)       VERSION="$2"; shift 2 ;;
            --out-dir)       OUT_DIR="$2"; shift 2 ;;
            --id)            TEST_ID="$2"; shift 2 ;;
            --build)         NEED_BUILD=1; shift ;;
            --elk-wipe)      NEED_ELK_WIPE=1; shift ;;
            -h|--help)   usage ;;
            *) echo "Unknown option: $1" >&2; usage ;;
        esac
    done

    # Reject missing or unknown modes before doing any work.
    case "$MODE" in
        serial|mt) ;;
        *) echo "ERROR: --mode is required and must be serial or mt" >&2; exit 1 ;;
    esac

    evssim_validate_version_arguments "$0" "$VERSION" 1
}

validate_path() {
    local name="$1"
    local path="$2"
    if [[ -z "$path" ]]; then
        echo "[run_onfi_test] ERROR: $name is not set" >&2
        exit 1
    fi
    if [[ ! -d "$path" ]]; then
        echo "[run_onfi_test] ERROR: $name ($path) is not a directory" >&2
        exit 1
    fi
}

validate_paths() {
    validate_path "ONFI_TEST_DIR" "$ONFI_TEST_DIR"
    validate_path "SIMULATOR_DIR" "$SIMULATOR_DIR"
    validate_path "LOGS_DIR" "$LOGS_DIR"
    validate_path "ELK_DIR" "$ELK_DIR"
    validate_path "BUILDER_DIR" "$BUILDER_DIR"
}

# Print test's parameters to both stdout and results file.
print_parameters() {
    local total_pages=$((PAGES * BLOCKS * FLASHES))

    echo "# ONFI Test Results Summary" > $RESULTS_FILE
    echo "
## Test Parameters

| Parameter   | Value                | Description                                       |
| ----------- | -------------------- | ------------------------------------------------- |
| mode        | $(printf "%-20s" $MODE) | onfi mode                                         |
| pages       | $(printf "%-20s" $PAGES) | number of pages per block                         |
| blocks      | $(printf "%-20s" $BLOCKS) | number of blocks per flash                        |
| flashes     | $(printf "%-20s" $FLASHES) | number of flashes per ssd device                  |
| total pages | $(printf "%-20s" $total_pages) | total pages in ssd device                         |
| ops         | $(printf "%-20s" $OPS) | number of operations to be executed in the test   |
| version     | $(printf "%-20s" $VERSION) | operational system version                        |

ONFI test command:

$(get_test_command)
" | tee -a $RESULTS_FILE

    echo "---" >> $RESULTS_FILE
}

# Print test's results to both stdout and results file.
print_results() {
    echo
    echo "============================================================================"

    {
        echo
        echo "## Test Results"
        echo
        printf -- "    %-22s = %s\n" "test_name"           "$RESULTS_FILE"
        [[ -n "$TEST_ID" ]] && printf -- "    %-22s = %s\n" "test_id"     "$TEST_ID"
        printf -- "    %-22s = %s\n" "test_start_time"     "$START_TIME"
        printf -- "    %-22s = %s\n" "test_end_time"       "$END_TIME"
        printf -- "    %-22s = %s\n" "wall_clock_time_ms"  "$TEST_WALL_CLOCK_TIME"
    } | tee -a $RESULTS_FILE

    grep -vF "[host_metrics]" "$ELK_READ_OUTPUT_FILE" | tee -a $RESULTS_FILE
    echo "" >> $RESULTS_FILE
}

is_number() {
    if [[ "$1" =~ ^[0-9]+$ ]]; then
        return 0
    fi
    return 1
}

set_results_file_name() {
    mkdir -p "$OUT_DIR"

    local id_flag=""
    [[ -n "$TEST_ID" ]] && id_flag="_test${TEST_ID}"

    RESULTS_FILE="$(realpath $OUT_DIR/onfi_test_summary_${MODE}${id_flag}_$(date +%y_%m_%d__%H_%M_%S).md)"
}

# Find the wall clock time from the gtest's output and set the global TEST_WALL_CLOCK_TIME.
set_test_wall_clock_time() {
    TEST_WALL_CLOCK_TIME=$(cat $TEST_OUTPUT_FILE | grep "1 test from 1 test case ran. (" | awk -F '[()]' '{print $2}' | awk '{print $1}')

    if ! is_number "$TEST_WALL_CLOCK_TIME"; then
        echo "[run_onfi_test] ERROR: can't parse wall-clock time from the test's output" >&2
        exit 1
    fi
}

# True when Elasticsearch answers (auth errors still count as "reachable").
elk_is_running() {
    curl -k -s --connect-timeout 3 -o /dev/null "$ES_URL" 2>/dev/null
}

# Start ELK if it not already up.
elk_start_if_needed() {
    echo
    echo "============================================================================"
    if elk_is_running; then
        echo "[run_onfi_test] ELK already running"
        NEED_ELK_MANAGE=0
        return
    fi

    if [[ ! -x "$ELK_INSTALL" ]]; then
        echo "[run_onfi_test] ERROR: $ELK_INSTALL not found" >&2
        exit 1
    fi

    echo "[run_onfi_test] starting ELK ..."
    NEED_ELK_MANAGE=1
    mkdir -p "$LOGS_DIR"
    $ELK_INSTALL "$LOGS_DIR" "$ELK_DIR"
    if [[ "$?" != 0 ]]; then
        echo "[run_onfi_test] ERROR: $ELK_INSTALL failed" >&2
        exit 1
    fi
    echo "[run_onfi_test] ELK started successfully"
}

# Remove the ELK stack, but only if this run started it, so a
# pre-existing stack is never disturbed.
elk_stop_if_needed() {
    if [[ "$NEED_ELK_MANAGE" -eq 0 ]]; then
        return
    fi

    echo
    echo "============================================================================"
    echo "[run_onfi_test] stopping ELK ..."
    $ELK_CLEAN --complete-cleanup
    if [[ "$?" != 0 ]]; then
        echo "[run_onfi_test] ERROR: $ELK_CLEAN failed" >&2
        exit 1
    fi
    echo "[run_onfi_test] ELK stopped successfully"
}

# Wipe all indexes from ELK and clear logs dir if needed.
elk_wipe_if_needed() {
    if [[ "$NEED_ELK_WIPE" -eq 0 ]]; then
        return
    fi

    echo
    echo "============================================================================"
    echo "[run_onfi_test] delete old log files ..."
    rm -rf "${LOGS_DIR:?}"/*
    if [[ "$?" != 0 ]]; then
        echo "[run_onfi_test] ERROR: delete olf log files failed" >&2
        exit 1
    fi
    echo "[run_onfi_test] clean all logs from ELK ..."
    $ELK_WIPE
    if [[ "$?" != 0 ]]; then
        echo "[run_onfi_test] ERROR: ELK cleanup failed" >&2
        exit 1
    fi

    echo "[run_onfi_test] ELK was cleaned successfully"
}

# Read relevant stats from ELK.
elk_read() {
    echo
    echo "============================================================================"
    echo "[run_onfi_test] read logs from ELK (can take several minutes) ..."
    $ELK_READ | tee "$ELK_READ_OUTPUT_FILE"
    local rc="${PIPESTATUS[0]}"
    if [[ "$rc" != 0 ]]; then
        echo "[run_onfi_test] ERROR: can't read logs from elk" >&2
        exit 1
    fi
    echo "[run_onfi_test] read logs from ELK successfully"
}

build_onfi_test_if_needed() {
    if [[ "$NEED_BUILD" == 1 || ! -x "$ONFI_TEST_BINARY" ]]; then
        echo;
        echo "============================================================================"
        echo "[run_onfi_test] building $ONFI_TEST_BINARY"
        echo
        pushd $BUILDER_DIR > /dev/null
        ./compile-host-tests.sh $VERSION
        local rc="$?"
        popd > /dev/null
        if [[ "$rc" != 0 ]]; then
            echo "[run_onfi_test] ERROR: build failed" >&2
            exit 1
        fi
    fi
}

get_test_command() {
    local args="--mode $MODE"
    [[ -n "$PAGES" ]]   && args="$args --pages $PAGES"
    [[ -n "$BLOCKS" ]]  && args="$args --blocks $BLOCKS"
    [[ -n "$FLASHES" ]] && args="$args --flashes $FLASHES"
    [[ -n "$OPS" ]]     && args="$args --ops $OPS"

    echo "$ONFI_TEST_BINARY $args"
}

run_test_in_evssim() {
    evssim_run_at_folder "$VERSION" "simulator/eVSSIM/tests/host/onfi_multithreaded" "$(get_test_command)"
}

run_onfi_test() {
    echo;
    echo "============================================================================"
    echo "[run_onfi_test] run onfi test ..."
    echo "$(get_test_command)"
    echo

    run_test_in_evssim | tee "$TEST_OUTPUT_FILE"
    local rc="${PIPESTATUS[0]}"

    if [[ "$rc" != "0" ]]; then
        echo "[run_onfi_test] ERROR: $ONFI_TEST_BINARY failed" >&2
        exit 1
    fi
}

on_exit_callback() {
    local rc="$?"
    trap - EXIT INT TERM
    rm -f "$TEST_OUTPUT_FILE" "$ELK_READ_OUTPUT_FILE"
    elk_stop_if_needed
    exit "$rc"
}


# =======================================
### Main ###

START_TIME=$(date +"%d/%m/%Y %H:%M:%S")
parse_args $@
validate_paths
trap on_exit_callback EXIT INT TERM

echo "============================================================================"
echo "[run_onfi_test] start (mode=$MODE)"
echo

set_results_file_name
print_parameters

build_onfi_test_if_needed
elk_start_if_needed
elk_wipe_if_needed

run_onfi_test

set_test_wall_clock_time
elk_read
END_TIME=$(date +"%d/%m/%Y %H:%M:%S")
print_results

echo
echo "============================================================================"
echo "[run_onfi_test] onfi test finished successfully"
echo "[run_onfi_test] results can be found in $RESULTS_FILE"
echo "[run_onfi_test] done"
