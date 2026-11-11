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

# Keep every cpu out of deep idle states for the whole benchmark (CPU_IDLE_HOLD=0 to measure with
# them): an idle CI host otherwise adds its C-state exit latency to every wakeup. The kernel honours
# the /dev/cpu_dma_latency request only while the fd stays open, so a privileged container (root,
# like the SCHED_FIFO row) holds it at 0 us and killing the container releases it.
echo "cpuidle: $(cat /sys/devices/system/cpu/cpuidle/current_driver 2>/dev/null) states:" \
     $(for s in /sys/devices/system/cpu/cpu0/cpuidle/state*; do echo "$(cat $s/name)=$(cat $s/latency)us"; done)
if [ "${CPU_IDLE_HOLD:-1}" = 1 ]; then
    IDLE_HOLD=$(docker run -d --rm --privileged --entrypoint /bin/bash "${COSLEEP_IMAGE:-evssim:latest}" \
        -c 'exec 3>/dev/cpu_dma_latency && printf 0 >&3 && exec sleep infinity')
    trap 'docker kill "$IDLE_HOLD" >/dev/null 2>&1 || true' EXIT
    sleep 1
    docker ps -q --no-trunc | grep -qx "$IDLE_HOLD" || { echo "cpu_dma_latency hold failed:"; docker logs "$IDLE_HOLD"; exit 2; }
    echo "deep C-states blocked for the benchmark (cpu_dma_latency=0, container ${IDLE_HOLD:0:12})"
fi

# 3. every configuration of the Scenario A / B matrix that runs on a normal boot (~25 min)
echo "host: $(nproc) cpus, load $(cut -d' ' -f1-3 /proc/loadavg), isolated=[$(cat /sys/devices/system/cpu/isolated)], cpu_idle_hold=${CPU_IDLE_HOLD:-1}"
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
