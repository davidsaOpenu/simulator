#!/bin/bash
#
# T0 - is the YABS integration actually producing disk numbers, or passing vacuously?
#
# The existing guest test (tests/guest/run_fs_benchmarks/test_fs_benchmark.py) asserts
# only that yabs.json and yabs.txt exist. YABS writes both of those even when it skips
# the disk benchmark entirely, so that assertion cannot distinguish "benchmarked" from
# "silently measured nothing".
#
# This script checks the thing that actually matters: whether the JSON contains a "fio"
# array with one entry per block size. It exits non-zero when it does not, and says
# which of the known silent-skip paths was taken.
#
# Usage: sudo ./yabs_integration_check.sh [mount_point]
#
set -uo pipefail

MOUNT_POINT=${1:-/mnt/exofs0}
YABS_DIR=${YABS_DIR:-/home/esd/yet-another-bench-script}
OUT=${OUTPUT_DIR:-/tmp/output}/yabs-check
JSON="$OUT/yabs.json"
TXT="$OUT/yabs.txt"

if [[ $EUID -ne 0 ]]; then echo "ERROR must run as root" >&2; exit 1; fi
if ! mountpoint -q "$MOUNT_POINT"; then echo "ERROR $MOUNT_POINT not mounted" >&2; exit 1; fi
if [[ ! -f "$YABS_DIR/yabs.sh" ]]; then echo "ERROR no yabs.sh at $YABS_DIR" >&2; exit 1; fi

mkdir -p "$OUT"

echo "=== preconditions ==="
avail_kb=$(df -k "$MOUNT_POINT" | awk 'NR==2{print $4}')
echo "filesystem : $(awk -v m="$MOUNT_POINT" '$2==m{print $3}' /proc/mounts)"
echo "available  : ${avail_kb} KB"
# yabs.sh:534 skips the disk test below 2 GiB; yabs.sh:458 then wants a 2 GB test file.
echo "yabs needs : 2097152 KB free (hard-coded gate) + a 2 GB fio file"
if [[ "$avail_kb" -lt 2097152 ]]; then
    echo "verdict    : BELOW GATE - yabs will skip the disk test"
else
    echo "verdict    : above gate"
fi
echo -n "local fio  : "; command -v fio >/dev/null && fio --version || echo "MISSING (yabs would try to download a binary)"
echo

echo "=== running yabs (-i -g -n, as the merged test does) ==="
cd "$MOUNT_POINT" || exit 1
bash "$YABS_DIR/yabs.sh" -ign -w "$JSON" > "$TXT" 2>&1
yabs_rc=$?
echo "yabs exit rc : $yabs_rc"
echo "json written : $([[ -f $JSON ]] && echo yes || echo no)"
echo "txt written  : $([[ -f $TXT ]] && echo yes || echo no)"
echo

echo "=== what the text report says about the disk test ==="
grep -iE "skipping disk test|dd Sequential|fio Disk Speed|Running fio" "$TXT" | sed 's/^/  /' || echo "  (no disk-test line found)"
echo

echo "=== what the JSON actually contains ==="
if [[ ! -f "$JSON" ]]; then
    echo "  no JSON at all"
    exit 1
fi
echo "  size: $(stat -c %s "$JSON") bytes"
echo "  top-level keys: $(sed 's/[{,]/\n/g' "$JSON" | grep -oE '^"[a-z_]+":' | tr -d '":' | tr '\n' ' ')"

if grep -q '"fio"' "$JSON"; then
    entries=$(grep -o '"bs":' "$JSON" | wc -l)
    echo "  fio array    : PRESENT with $entries block-size entries"
    echo
    if [[ "$entries" -ge 4 ]]; then
        echo "RESULT: integration produces disk numbers ($entries entries)"
        exit 0
    fi
    echo "RESULT: fio array present but only $entries entries (expected 4)"
    exit 1
fi

echo "  fio array    : ABSENT"
echo
echo "RESULT: YABS produced NO disk measurements."
if grep -qi "skipping disk test" "$TXT"; then
    echo "  cause: the 2 GiB free-space gate (yabs.sh:534)"
elif grep -qi "dd Sequential" "$TXT"; then
    echo "  cause: fio failed or was killed by the 35s timeout; yabs fell back to dd (yabs.sh:660)"
else
    echo "  cause: unknown - inspect $TXT"
fi
echo "  note: the merged test asserts only that these two files exist, so it passes anyway."
exit 1
