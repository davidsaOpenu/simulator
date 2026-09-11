#!/bin/bash
#
# T0 DoD - prove file data written to exofs actually reaches the device.
#
# Writes files of several shapes, unmounts, remounts, and compares byte for
# byte. Before T0 every one of these came back empty or short: exofs_writepages
# was a stub, so data was dirtied in the page cache and dropped at unmount.
#
# Also covers the append path, which needs a page whose contents came back from
# the device rather than from the cache -- that is the case exofs_write_begin
# used to zero.
#
# Usage: sudo ./writeback_roundtrip_test.sh [mount_point]
# Assumes the OSD target is up and exofs has been mkfs'd (see
# exofs/run_osd_emulator_and_mount_exofs.sh).
#
set -uo pipefail

MOUNT_POINT=${1:-/mnt/exofs0}
OSD_DEV=${OSD_DEV:-/dev/osd0}
PID=${PID:-0x10000}
WORK="$MOUNT_POINT/t0"

failures=0
checks=0

pass() { checks=$((checks + 1)); echo "  PASS  $1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); echo "  FAIL  $1"; }

remount() {
    sync
    if ! umount "$MOUNT_POINT"; then
        echo "FATAL umount failed"; exit 1
    fi
    if ! mount -t exofs -o "pid=$PID" "$OSD_DEV" "$MOUNT_POINT"; then
        echo "FATAL remount failed"; exit 1
    fi
}

if [[ $EUID -ne 0 ]]; then
    echo "ERROR must run as root" >&2
    exit 1
fi
if ! mountpoint -q "$MOUNT_POINT"; then
    echo "ERROR $MOUNT_POINT is not mounted" >&2
    exit 1
fi

dmesg -c > /dev/null 2>&1 || true
rm -rf "$WORK"
mkdir -p "$WORK"

# ---------------------------------------------------------------- write pass
# Sizes cover: sub-page, exactly one page, page-straddling, and multi-page.
# The two exact-page-multiple cases (4096, 262144) are deliberate: they are the
# sizes the QEMU KV retrieve off-by-one used to make unreadable, so they guard
# that fix as well as writeback itself.
declare -A EXPECTED
REF=/tmp/t0-ref
rm -rf "$REF"; mkdir -p "$REF"
for spec in "tiny:100" "page:4096" "straddle:10000" "pages16:65536" "pages32:131072" "big:262144"; do
    name=${spec%%:*}
    size=${spec##*:}
    # Raw urandom: non-trivial content, and cheap enough to generate under TCG.
    # Keep a reference copy off the filesystem under test so a mismatch can be
    # located, not just detected.
    head -c "$size" /dev/urandom > "$REF/$name"
    cp "$REF/$name" "$WORK/$name"
    actual=$(stat -c %s "$WORK/$name")
    if [[ "$actual" != "$size" ]]; then
        echo "FATAL could not create $name at $size bytes (got $actual)"; exit 1
    fi
    EXPECTED[$name]=$(md5sum < "$REF/$name" | cut -d' ' -f1)
done

echo "> Wrote ${#EXPECTED[@]} files; unmounting and remounting..."
remount

# --------------------------------------------------------------- verify pass
echo "> Verifying contents survived the round trip"
for name in "${!EXPECTED[@]}"; do
    if [[ ! -f "$WORK/$name" ]]; then
        fail "$name: file missing after remount"
        continue
    fi
    got=$(md5sum < "$WORK/$name" | cut -d' ' -f1)
    if [[ "$got" == "${EXPECTED[$name]}" ]]; then
        pass "$name: $(stat -c %s "$WORK/$name") bytes match"
    else
        # Locate the divergence: a tail truncation, a hole at a page boundary and
        # a scrambled buffer are three different bugs.
        diffinfo=$(cmp "$REF/$name" "$WORK/$name" 2>&1 | head -1)
        firstbyte=$(echo "$diffinfo" | sed -n 's/.*byte \([0-9]*\).*/\1/p')
        fail "$name: content differs (size $(stat -c %s "$WORK/$name")); $diffinfo"
        if [[ -n "$firstbyte" ]]; then
            page=$(( (firstbyte - 1) / 4096 ))
            off=$(( (firstbyte - 1) % 4096 ))
            echo "        first difference at page $page offset $off"
            echo "        expected: $(dd if="$REF/$name" bs=1 skip=$((firstbyte - 1)) count=16 2>/dev/null | od -An -tx1 | tr -s ' ')"
            echo "        actual:   $(dd if="$WORK/$name" bs=1 skip=$((firstbyte - 1)) count=16 2>/dev/null | od -An -tx1 | tr -s ' ')"
        fi
    fi
done

# ---------------------------------------------------------------- append test
# The page holding the tail is not in the cache after a remount, so write_begin
# must fetch it from the device instead of zeroing it.
echo "> Testing append to a file whose pages are not cached"
head_content="HEAD-CONTENT-MUST-SURVIVE"
tail_content="-APPENDED-TAIL"
echo -n "$head_content" > "$WORK/append"
remount
echo -n "$tail_content" >> "$WORK/append"
remount
got=$(cat "$WORK/append")
if [[ "$got" == "${head_content}${tail_content}" ]]; then
    pass "append: prefix preserved across remount"
else
    fail "append: expected '${head_content}${tail_content}', got '${got}'"
fi

# ------------------------------------------------------------------- kernel log
echo "> Checking dmesg for warnings and oopses"
log=$(dmesg 2>/dev/null || true)
if echo "$log" | grep -qE "WARNING:|BUG:|Oops|call trace"; then
    fail "dmesg contains a warning or oops"
    echo "$log" | grep -E -A 5 "WARNING:|BUG:|Oops|call trace" | head -40
else
    pass "dmesg clean"
fi

rm -rf "$WORK"
sync

echo
echo "=== $((checks - failures))/$checks checks passed ==="
[[ $failures -eq 0 ]]
