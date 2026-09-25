#!/usr/bin/env bash
# Sleep-mechanism sweep on QEMU's real main loop: co_sleep vs hybrid vs spin, N coroutines at once.
#   co_sleep  qemu_co_sleep_ns() for the whole delay
#   hybrid    qemu_co_sleep_ns() to target - slack, then busy-spin inside the coroutine (slack 20 us)
#   spin      busy-spin the whole delay inside the coroutine (blocks the main loop; here for contrast)
# Each run: $COS coroutines x 50,000 sleeps at 132 us and 982 us; the demo prints wall time, ideal
# time (50,000 x req) and main-loop CPU share per delay. Output: $RESULTS/<label>.raw.csv + .log.
# Run once with COS=3 and once with COS=16 for the tables in docs/RUNS_SUMMARY.md §5.
set -euo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COS="${COS:-3}"; SAMPLES="${SAMPLES:-50000}"; DELAYS="${DELAYS:-132,982}"
RESULTS="${RESULTS:-$R/results_demo_hybrid/${COS}co}"
SKIP="${SKIP:-}"            # space-separated labels to skip, e.g. SKIP="sp16 co16-iso hy16-iso" (CI)
mkdir -p "$RESULTS"
[ -x "$R/bin/qemu_mainloop_demo_patched" ] || "$R/scripts/build_demo.sh"

PIDS=()
noise_full() { for c in 0 1 2 3 4 5;    do "$R/bin/worker_cpu_bound"  --cpu $c --quiet >/dev/null 2>&1 & PIDS+=($!); done
               for c in 6 7 8 9 10;     do "$R/bin/worker_io_bound"   --cpu $c --quiet >/dev/null 2>&1 & PIDS+=($!); done
               for c in 11 12 13 14 15; do "$R/bin/worker_mixed_5050" --cpu $c --quiet >/dev/null 2>&1 & PIDS+=($!); done; sleep 2; }
stop() { [ ${#PIDS[@]} -gt 0 ] && { kill "${PIDS[@]}" 2>/dev/null || true; wait "${PIDS[@]}" 2>/dev/null || true; }; PIDS=(); }
trap stop EXIT INT TERM

run() { # label mechanism noise(none|full) [extra demo args...]
    local label=$1 mech=$2 noise=$3; shift 3
    if tr " " "\n" <<<"$SKIP" | grep -qx -- "$label"; then echo "== $label skipped"; return 0; fi
    echo "== $label mech=$mech cos=$COS noise=$noise $*  load=$(cut -d' ' -f1 /proc/loadavg) $(date +%T)"
    [ "$noise" = full ] && noise_full
    "$R/bin/qemu_mainloop_demo_patched" --variant patched --mechanism "$mech" --coroutines "$COS" \
        --samples "$SAMPLES" --delays-us "$DELAYS" --run-label "$label" "$@" \
        --raw "$RESULTS/$label.raw.csv" > "$RESULTS/$label.log" 2>&1
    stop; grep -E ' all |RUN' "$RESULTS/$label.log"; sleep 2
}
C=$COS
run co$C      co_sleep none
run hy$C      hybrid   none
run hy$C-s5   hybrid   none --slack-us 5
run co$C-full co_sleep full
run hy$C-full hybrid   full
run co$C-iso  co_sleep none --pin-cpu 1      # meaningful only on the isolcpus boot
run hy$C-iso  hybrid   none --pin-cpu 1
run sp$C      spin     none                  # takes COS x the ideal time: ~15 min at COS=16
echo "== DONE $(date +%T) -> $RESULTS"
