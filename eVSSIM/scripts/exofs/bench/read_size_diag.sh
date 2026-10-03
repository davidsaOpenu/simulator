#!/bin/bash
#
# Isolate why files at or above 128K read back as zeroes.
#
# Writes one file below the boundary and one above, remounts so nothing can be
# served from the page cache, then reads each under its own ftrace marker. The
# Key-Value commands issued during each read say which of three things is
# happening: no command at all (the read never reaches the device), a command
# that fails, or a command that succeeds but returns short.
#
set -uo pipefail
M=${1:-/mnt/exofs0}
OSD_DEV=${OSD_DEV:-/dev/osd0}
PID=${PID:-0x10000}
TRACE_DIR=/sys/kernel/debug/tracing
OUT=${OUTPUT_DIR:-/tmp/output}/readdiag
mkdir -p "$OUT"

echo "> writing 64K and 128K"
head -c 65536  /dev/urandom > "$M/f64"
head -c 131072 /dev/urandom > "$M/f128"
sync
md5_64=$(md5sum < "$M/f64" | cut -d' ' -f1)
md5_128=$(md5sum < "$M/f128" | cut -d' ' -f1)
stat -c 'inode %i  size %s  %n' "$M/f64" "$M/f128" | tee "$OUT/inodes.txt"

echo "> remount"
umount "$M" || { echo "FATAL umount"; exit 1; }
mount -t exofs -o "pid=$PID" "$OSD_DEV" "$M" || { echo "FATAL mount"; exit 1; }

echo 0 > "$TRACE_DIR/tracing_on"
echo nop > "$TRACE_DIR/current_tracer"
echo 8192 > "$TRACE_DIR/buffer_size_kb" 2>/dev/null || true
echo > "$TRACE_DIR/trace"
echo 1 > "$TRACE_DIR/tracing_on"

echo "PHASE=read64"  > "$TRACE_DIR/trace_marker"
got_64=$(md5sum < "$M/f64" | cut -d' ' -f1)
echo "PHASE=read128" > "$TRACE_DIR/trace_marker"
got_128=$(md5sum < "$M/f128" | cut -d' ' -f1)
echo "PHASE=end"     > "$TRACE_DIR/trace_marker"

echo 0 > "$TRACE_DIR/tracing_on"
cp "$TRACE_DIR/trace" "$OUT/trace.txt"

echo
echo "  64K  expected $md5_64"
echo "  64K  got      $got_64   $([ "$md5_64" = "$got_64" ] && echo MATCH || echo DIFFER)"
echo " 128K  expected $md5_128"
echo " 128K  got      $got_128   $([ "$md5_128" = "$got_128" ] && echo MATCH || echo DIFFER)"
echo
echo "=== KV commands during the 64K read ==="
awk '/PHASE=read64/{f=1;next} /PHASE=read128/{f=0} f' "$OUT/trace.txt" | grep -o "exofs_kv .*" || echo "  (none)"
echo "=== KV commands during the 128K read ==="
awk '/PHASE=read128/{f=1;next} /PHASE=end/{f=0} f' "$OUT/trace.txt" | grep -o "exofs_kv .*" || echo "  (none)"
echo
echo "=== exofs/nvme dmesg tail ==="
dmesg | grep -iE "exofs|nvme|__nvme_submit" | tail -20
