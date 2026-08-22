#!/bin/bash
# Round-trip check for the exofs write path.
#
# Writes files of several sizes, forces them out to the device, drops the page
# cache, rewrites part of one of them, then unmounts and remounts and compares
# every file byte-for-byte against a copy kept on the root filesystem.
#
# Exits non-zero on any mismatch, read error, or kernel WARN/BUG/oops.

MOUNT_POINT=${MOUNT_POINT:-/mnt/exofs0}
EXOFS_DEV=${EXOFS_DEV:-/dev/osd0}
EXOFS_PID=${EXOFS_PID:-0x10000}
SRC=$(mktemp -d)
trap 'rm -rf "$SRC"' EXIT
SIZES="100 4096 8192"
rc=0

fail() { echo "FAIL: $*"; rc=1; }

# Every exit goes through here, so the RESULT: marker is always printed and the
# exit status always reflects rc - including on the early mount/umount paths.
finish() {
    if [ $rc -eq 0 ]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; fi
    exit $rc
}

is_mounted() { grep -qs " $MOUNT_POINT " /proc/mounts; }

remount() {
    umount "$MOUNT_POINT" || { fail "umount failed"; return 1; }
    mount -t exofs -o "pid=$EXOFS_PID" "$EXOFS_DEV" "$MOUNT_POINT" \
        || { fail "mount failed"; return 1; }
}

echo "== exofs write round-trip =="
dmesg -c > /dev/null

is_mounted || \
    mount -t exofs -o "pid=$EXOFS_PID" "$EXOFS_DEV" "$MOUNT_POINT" \
    || { fail "initial mount failed"; finish; }

# 1. Write one file per size and push it to the device.
for s in $SIZES; do
    head -c "$s" /dev/urandom > "$SRC/f$s" || fail "could not build source f$s"
    cp "$SRC/f$s" "$MOUNT_POINT/f$s" || fail "write of f$s failed"
done
sync

# 2. Drop the page cache so the partial rewrite below, and the final
#    comparison, have to go to the device instead of being served from memory.
#    Note this does NOT exercise releasepage/invalidatepage: every call site
#    is gated on page_has_private(), and exofs never sets PagePrivate.
echo 3 > /proc/sys/vm/drop_caches

# 3. Partial-page rewrite of a file that now only exists on the device. Offset
#    4090 straddles the page boundary, so this is a read-modify-write of two
#    pages, one of them partial. The same edit is applied to the reference copy
#    in $SRC so the comparison below still has something to compare against.
for target in "$MOUNT_POINT/f8192" "$SRC/f8192"; do
    printf 'PARTIAL-REWRITE' | \
        dd of="$target" bs=1 seek=4090 conv=notrunc 2>/dev/null \
        || fail "partial rewrite of $target failed"
done
sync

# 4. Round-trip.
remount || finish

for s in $SIZES; do
    if cmp "$SRC/f$s" "$MOUNT_POINT/f$s"; then
        echo "ok: f$s matches after remount"
    else
        fail "f$s differs after remount"
    fi
done

# 5. Evidence that *file data* reached the device, not just directory and
#    inode metadata. dir.c stores directory buffers through the same helper,
#    so a bare call count proves nothing - it is already non-zero with the
#    write path stubbed out. dir.c only ever stores whole directory chunks,
#    so a 100-byte store can only be f100's file data.
stores=$(dmesg | grep -c 'exofs_set_obj_data: obj.id=')
echo "KV store commands issued: $stores"
dmesg | grep -qE 'exofs_set_obj_data:.*data size=100$' \
    || fail "no KV store of file data (100 bytes) in dmesg"

# 6. No kernel complaints anywhere in the run.
if dmesg | grep -iE 'WARNING:|BUG:|Oops|Call Trace'; then
    fail "kernel WARN/BUG/oops in dmesg"
fi

finish
