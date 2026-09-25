#!/usr/bin/env bash
# Run the standalone demo (bin/qemu_mainloop_demo_*) across the spike's Scenario A / B matrix.
# No guest, no reboot: everything here runs on the normal boot. The isolated-boot rows are run
# separately with ISOLATED=1 once the host is booted per docs/hard_isolation_howto.md.
#
#   stock   = util/main-loop.c as in the qemu tree (the millisecond rounding)
#   patched = with scripts/qemu_main_loop_ns_deadline.patch
# Each run: 3 coroutines x 50,000 sleeps per delay (132 us and 982 us), main-loop thread on cpu 1
# when pinned (its SMT sibling is cpu 9). Host noise comes from ../workers (cpu / io / mixed).
#
# Output: $RESULTS/<label>.raw.csv (+ .log). Summarise with scripts/demo_report.py.
set -euo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVSSIM_ROOT="$(cd "$R/../.." && pwd)"
RESULTS="${RESULTS:-$R/results_demo}"
SAMPLES="${SAMPLES:-50000}"; COS="${COS:-3}"; DELAYS="${DELAYS:-132,982}"
IMAGE="${COSLEEP_IMAGE:-evssim:latest}"
ONLY="${ONLY:-}"            # space-separated labels to run; empty = all applicable
mkdir -p "$RESULTS"

[ -x "$R/bin/qemu_mainloop_demo_patched" ] || "$R/scripts/build_demo.sh"

NOISE_PIDS=()
start_noise() {   # $1 = "full" | "half" | "" ; $2.. = type@cpu specs
    local mode="$1"; shift
    case "$mode" in
        full) for c in 0 1 2 3 4 5;    do "$R/bin/worker_cpu_bound"  --cpu $c --quiet >/dev/null 2>&1 & NOISE_PIDS+=($!); done
              for c in 6 7 8 9 10;     do "$R/bin/worker_io_bound"   --cpu $c --quiet >/dev/null 2>&1 & NOISE_PIDS+=($!); done
              for c in 11 12 13 14 15; do "$R/bin/worker_mixed_5050" --cpu $c --quiet >/dev/null 2>&1 & NOISE_PIDS+=($!); done ;;
        half) for c in 0 1 2 3 4 5 6 7; do "$R/bin/worker_cpu_bound" --cpu $c --quiet >/dev/null 2>&1 & NOISE_PIDS+=($!); done ;;
        housekeeping)   # every cpu that is NOT isolated (isolcpus=1-3,9-11): cpu-bound 0,4,5,6 · io 7,8 · mixed 12-15
              for c in 0 4 5 6;       do "$R/bin/worker_cpu_bound"  --cpu $c --quiet >/dev/null 2>&1 & NOISE_PIDS+=($!); done
              for c in 7 8;           do "$R/bin/worker_io_bound"   --cpu $c --quiet >/dev/null 2>&1 & NOISE_PIDS+=($!); done
              for c in 12 13 14 15;   do "$R/bin/worker_mixed_5050" --cpu $c --quiet >/dev/null 2>&1 & NOISE_PIDS+=($!); done ;;
        "") ;;
    esac
    for spec in "$@"; do
        case "${spec%@*}" in cpu) b=worker_cpu_bound ;; io) b=worker_io_bound ;; mixed) b=worker_mixed_5050 ;; esac
        "$R/bin/$b" --cpu "${spec#*@}" --quiet >/dev/null 2>&1 & NOISE_PIDS+=($!)
    done
    [ ${#NOISE_PIDS[@]} -gt 0 ] && sleep 2
    return 0
}
stop_noise() {
    [ ${#NOISE_PIDS[@]} -eq 0 ] && return 0
    kill "${NOISE_PIDS[@]}" 2>/dev/null || true; wait "${NOISE_PIDS[@]}" 2>/dev/null || true; NOISE_PIDS=()
}
trap stop_noise EXIT INT TERM

# run LABEL SCENARIO ISOLATION COLOCATION IFTYPE VARIANT DRIVER PIN(-1|cpu) RT(0|1) NOISEMODE [spec...]
run() {
    local label=$1 scen=$2 iso=$3 colo=$4 ift=$5 variant=$6 driver=$7 pin=$8 rt=$9 mode=${10}; shift 10
    if [ -n "$ONLY" ] && ! tr " " "\n" <<<"$ONLY" | grep -qx -- "$label"; then return 0; fi
    echo "== $label  variant=$variant driver=$driver pin=$pin rt=$rt noise='$mode $*'  load=$(cut -d' ' -f1-3 /proc/loadavg)  $(date -Is)"
    start_noise "$mode" "$@"
    local args=(--driver "$driver" --delays-us "$DELAYS" --samples "$SAMPLES" --coroutines "$COS"
                --variant "$variant" --run-label "$label" --scenario "$scen" --isolation "$iso"
                --colocation "$colo" --iftype "$ift")
    [ "$pin" -ge 0 ] && args+=(--pin-cpu "$pin")
    if [ "$rt" = 1 ]; then
        # SCHED_FIFO needs root: run the same binary inside the privileged builder container
        docker run --rm --privileged --entrypoint /bin/bash -v "$EVSSIM_ROOT":/evssim -v "$R":/work -v "$RESULTS":/results -w /work "$IMAGE" -lc \
            "chrt -f 1 ./bin/qemu_mainloop_demo_$variant $(printf '%q ' "${args[@]}") --raw /results/$label.raw.csv" \
            > "$RESULTS/$label.log" 2>&1
        chown "$(id -u):$(id -g)" "$RESULTS/$label.raw.csv" 2>/dev/null || true
    else
        "$R/bin/qemu_mainloop_demo_$variant" "${args[@]}" --raw "$RESULTS/$label.raw.csv" > "$RESULTS/$label.log" 2>&1
    fi
    stop_noise
    grep ' all ' "$RESULTS/$label.log" | sed 's/^/   /'
    sleep 3
}

if [ "${ISOLATED:-0}" = 1 ]; then
    [ "$(cat /sys/devices/system/cpu/isolated)" = "1-3,9-11" ] || { echo "host is not booted with isolcpus=1-3,9-11" >&2; exit 2; }
    run iso-unpinned      B hard none        none  patched main_loop -1 0 ""
    run iso-pinned        B hard none        none  patched main_loop  1 0 ""
    run iso-pinned-full   A hard none        mixed patched main_loop  1 0 housekeeping
    exit 0
fi

# --- Scenario B: nothing else running
run stock-B-unpinned      B none none        none  stock   main_loop -1 0 ""
run aiopoll-B-unpinned    B none none        none  patched aio_poll  -1 0 ""
run B-unpinned            B none none        none  patched main_loop -1 0 ""
run B-softpin             B soft none        none  patched main_loop  1 0 ""
# --- Scenario A: one neighbour, per placement and type
run A-adjacent-unpinned   A none adjacent    mixed patched main_loop -1 0 "" cpu@5 io@6 mixed@7
run A-adjacent-pinned     A soft adjacent    mixed patched main_loop  1 0 "" cpu@5 io@6 mixed@7
run A-smt-cpu             A soft smt_sibling cpu   patched main_loop  1 0 "" cpu@9
run A-shared-io           A soft shared_core io    patched main_loop  1 0 "" io@1
run A-shared-mixed        A soft shared_core mixed patched main_loop  1 0 "" mixed@1
run A-shared-cpu          A soft shared_core cpu   patched main_loop  1 0 "" cpu@1
# --- Scenario A: whole host loaded
run A-half-unpinned       A none shared_core cpu   patched main_loop -1 0 half
run A-full-unpinned       A none shared_core mixed patched main_loop -1 0 full
run A-full-softpin        A soft shared_core mixed patched main_loop  1 0 full
run A-full-rt             A none shared_core mixed patched main_loop -1 1 full
echo "== DONE $(date -Is) -> $RESULTS"
