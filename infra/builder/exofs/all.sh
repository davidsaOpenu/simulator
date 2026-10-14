#!/bin/bash
# One-shot driver for the standalone exofs guest test:
#
#   docker images -> qemu (fixed commit) -> kernel 7.0.2 -> tools -> image
#   -> boot check -> full exofs test suite
#
# Steps whose artifacts already exist are skipped (FORCE=1 rebuilds all, or
# FORCE="kernel image" rebuilds only those). Everything is logged to
# ~/logs/exofs/all-<date>.log and a one-line-per-step summary is kept in
# ~/logs/exofs/STATUS, so the run can be followed or reviewed later.
#
# Usage (from infra/builder):  ./exofs/all.sh
set -o pipefail

EXOFS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILDER_DIR="$(cd "$EXOFS_DIR/.." && pwd)"
ROOT="$(readlink -f "$BUILDER_DIR/../../..")"
LOG_ROOT="$ROOT/logs/exofs"
mkdir -p "$LOG_ROOT"
STAMP="$(date +'%Y-%m-%d-%H-%M-%S')"
LOG="$LOG_ROOT/all-$STAMP.log"
STATUS="$LOG_ROOT/STATUS"
FORCE="${FORCE:-}"
DIST="$ROOT/${EXOFS_DIST_FOLDER:-dist-exofs}"
QEMU_TREE="$ROOT/${EXOFS_QEMU_FOLDER:-qemu-exofs}"

exec > >(tee -a "$LOG") 2>&1
echo "exofs all.sh started $(date) on $(hostname), log: $LOG"
printf 'run %s started %s\n' "$STAMP" "$(date +%T)" > "$STATUS"

cd "$BUILDER_DIR" || exit 1

forced() {
    [ "$FORCE" = "1" ] || [[ " $FORCE " == *" $1 "* ]]
}

step() {
    local name="$1" done_check="$2"
    shift 2
    if ! forced "$name" && eval "$done_check"; then
        echo "==== [$name] already done, skipping (FORCE=\"$name\" to redo)"
        printf '%-8s SKIPPED (already built)\n' "$name" >> "$STATUS"
        return 0
    fi
    echo
    echo "==== [$name] $(date +%T): $*"
    local start=$SECONDS
    "$@"
    local rc=$?
    local took=$((SECONDS - start))
    if [ $rc -eq 0 ]; then
        printf '%-8s OK      (%ds)\n' "$name" "$took" >> "$STATUS"
        echo "==== [$name] OK after ${took}s"
    else
        printf '%-8s FAILED  rc=%d (%ds) - see %s\n' "$name" "$rc" "$took" "$LOG" >> "$STATUS"
        echo "==== [$name] FAILED rc=$rc after ${took}s"
        printf 'run %s finished %s: FAILED at %s\n' "$STAMP" "$(date +%T)" "$name" >> "$STATUS"
        exit $rc
    fi
}

docker_images_ok() {
    docker image inspect evssim:ubuntu-14.04 >/dev/null 2>&1 && \
        docker image inspect evssim:ubuntu-26.04 >/dev/null 2>&1
}

step docker 'docker_images_ok' ./build-docker-image.sh
# qemu and kernel are redone when the fixed commit in env-exofs.sh changes
step qemu   'bash ./exofs/build.sh qemu-is-current >/dev/null 2>&1' bash ./exofs/build.sh qemu
step kernel 'bash ./exofs/build.sh kernel-is-current >/dev/null 2>&1' bash ./exofs/build.sh kernel
step tools  '[ -x "$DIST/nvme" ] && [ -x "$DIST/mkfs.exofs" ]' bash ./exofs/build.sh tools
step image  '[ -f "$DIST/system.img.template" ]' bash ./exofs/build.sh image
step boot   'false' bash ./exofs/run.sh boot
step test   'false' bash ./exofs/run.sh

printf 'run %s finished %s: ALL OK\n' "$STAMP" "$(date +%T)" >> "$STATUS"
echo
echo "==== exofs: everything passed. Summary:"
cat "$STATUS"
