#!/bin/bash
#
# Directory listings must agree before and after remount.
#
# Directory contents are served from the page cache, and exofs keeps those pages
# in step with every store by hand. A missed update is invisible while the cache
# is warm and wrong once the pages come back from the device, or the reverse. So
# change a directory in each way dir.c supports -- add, delete, rename over an
# existing name, move a subdirectory so its ".." is rewritten -- check the
# listing, remount, and check it again.
#
# The first phase pushes one directory past a single page, which no other test
# does: every directory in the benchmarks fits in one.
#
# Usage: sudo ./dir_consistency_test.sh [mount_point]
#
set -uo pipefail

MOUNT_POINT=${1:-/mnt/exofs0}
OSD_DEV=${OSD_DEV:-/dev/osd0}
PID=${PID:-0x10000}
WORK="$MOUNT_POINT/dirtest"
ENTRIES=${ENTRIES:-120}

failures=0
checks=0
pass() { checks=$((checks + 1)); echo "  PASS  $1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); echo "  FAIL  $1"; }

[[ $EUID -eq 0 ]]            || { echo "ERROR must run as root" >&2; exit 1; }
mountpoint -q "$MOUNT_POINT" || { echo "ERROR $MOUNT_POINT is not mounted" >&2; exit 1; }

# Drop the page cache but keep inodes and dentries, so directory pages have to
# be read back for inodes this mount created -- a case remount never exercises,
# because remount brings every inode back through exofs_iget().
evict() { sync; echo 1 > /proc/sys/vm/drop_caches; }

remount() {
    sync
    umount "$MOUNT_POINT" || { echo "FATAL umount failed"; exit 1; }
    mount -t exofs -o "pid=$PID" "$OSD_DEV" "$MOUNT_POINT" || { echo "FATAL remount failed"; exit 1; }
}

# listing LABEL NAME... -- $WORK must list exactly these names
listing() {
    local label=$1 want got
    shift
    want=$(printf '%s\n' "$@" | sort)
    got=$(ls -1A "$WORK" | sort)
    if [[ "$want" == "$got" ]]; then
        pass "$label"
    else
        fail "$label: $(diff <(echo "$want") <(echo "$got") | grep -c '^[<>]') names differ"
    fi
}

# same_inode LABEL A B -- A and B must be the same directory
same_inode() {
    if [[ "$(stat -c %i "$2" 2>/dev/null)" == "$(stat -c %i "$3" 2>/dev/null)" ]]; then
        pass "$1"
    else
        fail "$1"
    fi
}

verify() {
    local when=$1
    listing "$when: listing matches" "${expected[@]}"
    same_inode "$when: moved child's .. is its new parent" "$WORK/b/child/.." "$WORK/b"
    [[ "$(cat "$WORK/dst" 2>/dev/null)" == "renamed" ]] \
        && pass "$when: rename replaced the target" || fail "$when: rename replaced the target"
}

rm -rf "$WORK"
mkdir -p "$WORK"

echo "> add $ENTRIES entries (past one directory page)"
expected=()
for i in $(seq 1 "$ENTRIES"); do
    touch "$WORK/entry_with_a_longer_name_$i"
    expected+=("entry_with_a_longer_name_$i")
done
listing "add: all $ENTRIES entries listed" "${expected[@]}"

echo "> delete every third entry"
kept=()
for i in $(seq 1 "$ENTRIES"); do
    if (( i % 3 == 0 )); then rm "$WORK/entry_with_a_longer_name_$i"
    else kept+=("entry_with_a_longer_name_$i"); fi
done
expected=("${kept[@]}")
listing "delete: remaining entries listed" "${expected[@]}"

echo "> rename over an existing name, move a subdirectory"
echo renamed > "$WORK/src"
echo original > "$WORK/dst"
mv "$WORK/src" "$WORK/dst"
mkdir "$WORK/a" "$WORK/b" "$WORK/a/child"
mv "$WORK/a/child" "$WORK/b/"
expected+=(dst a b)

echo "> rmdir must refuse a non-empty directory"
if rmdir "$WORK/b" 2>/dev/null; then fail "rmdir refused non-empty"; else pass "rmdir refused non-empty"; fi

verify cached
evict
verify "after page-cache eviction"
remount
verify "after remount"

echo "> empty and remove a directory, then remount"
rmdir "$WORK/b/child" && rmdir "$WORK/b" && pass "rmdir of emptied directory" || fail "rmdir of emptied directory"
expected=("${kept[@]}" dst a)
listing "cached: removed directory gone" "${expected[@]}"
evict
listing "after page-cache eviction: removed directory gone" "${expected[@]}"
remount
listing "after remount: removed directory gone" "${expected[@]}"

rm -rf "$WORK"

if dmesg | grep -qE "BUG:|Oops|Call Trace"; then fail "dmesg clean"; else pass "dmesg clean"; fi
echo "=== $((checks - failures))/$checks checks passed ==="
[[ $failures -eq 0 ]]
