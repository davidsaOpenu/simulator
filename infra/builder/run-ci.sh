#!/bin/bash
# CI for the real-time delay spike: build QEMU, then benchmark qemu_co_sleep_ns() through QEMU's
# real main loop in every host configuration that needs no reboot, and print one summary report.
#
# What it does NOT do (compared with the previous run-ci.sh): tox, kernel, guest image, host /
# guest / exofs tests, ELK. Hard-isolated cores (isolcpus + nohz_full) are excluded on purpose:
# they need a reboot; see simulator/spike_delay_inject/docs/hard_isolation_howto.md for that run.
#
# Output: simulator/spike_delay_inject/results_ci/{matrix,mechanisms-3co,mechanisms-16co}/<run>.raw.csv + .log
#         and results_ci/report-*.md (also printed at the end). Total ~40 min after the QEMU build.
set -Eeo pipefail
trap 'ec=$?; echo "[run-ci.sh] FAILED with exit $ec on: $BASH_COMMAND" >&2' ERR

cd "$(dirname "${BASH_SOURCE[0]}")"
./check_tools.sh
source ./env.sh

SPIKE="$EVSSIM_ROOT_PATH/simulator/spike_delay_inject"
RESULTS="${RESULTS:-$SPIKE/results_ci}"

# 1. build environment + QEMU 2.12 (libqemuutil.a is what the benchmark links against)
./build-docker-image.sh
./compile-qemu.sh ubuntu-14.04

# 2. the benchmark binary, twice: stock util/main-loop.c and with the fix applied
"$SPIKE/scripts/build_demo.sh"

# 3. every configuration of the Scenario A / B matrix that runs on a normal boot (~25 min)
echo "host: $(nproc) cpus, load $(cut -d' ' -f1-3 /proc/loadavg), isolated=[$(cat /sys/devices/system/cpu/isolated)]"
RESULTS="$RESULTS/matrix" "$SPIKE/scripts/run_demo_matrix.sh"

# 4. sleep mechanisms, co_sleep vs hybrid, with 3 and with 16 coroutines in flight (~2 x 5 min).
#    Skipped here: pure spin (serialises the coroutines - 15 min at 16 of them, and already rejected)
#    and the isolated-cpu rows (need the isolcpus boot).
for cos in 3 16; do
    COS=$cos RESULTS="$RESULTS/mechanisms-${cos}co" SKIP="sp$cos co$cos-iso hy$cos-iso" \
        "$SPIKE/scripts/run_demo_mechanisms.sh"
done

# 5. summary
echo
echo "==================== real-time delay benchmark: summary ===================="
echo "--- Scenario A / B matrix (qemu_co_sleep_ns through the main loop; stock vs patched)"
"$SPIKE/scripts/demo_report.py" "$RESULTS/matrix" --md "$RESULTS/report-matrix.md"
for cos in 3 16; do
    echo "--- sleep mechanism, $cos coroutines in flight (hybrid is the decided default)"
    "$SPIKE/scripts/demo_report.py" "$RESULTS/mechanisms-${cos}co" --md "$RESULTS/report-mechanisms-${cos}co.md"
done
echo "raw data and per-run logs: $RESULTS"
