#!/bin/bash
#
# T4 — exofs object inventory workload.
#
# Drives every object type exofs creates while CONFIG_EXOFS_KV_TRACE records one
# ftrace line per NVMe Key-Value command. Pair with parse_kv_trace.py, which turns
# the trace into a per-type table.
#
# Assumes exofs is already mounted (see exofs/run_osd_emulator_and_mount_exofs.sh).
# Phases are delimited with ftrace markers so the parser can attribute commands to
# the operation that caused them.
#
# Usage:  sudo ./object_inventory_workload.sh [mount_point]
# Env:    OUTPUT_DIR   where to write trace.txt / inode_map.txt (default /tmp/output/inventory)
#         SMALL_FILES  number of small files to create (default 64)
#         SMALL_SIZE   size of each small file in bytes (default 512)
#         LARGE_SIZE   size of the large file in KB (default 1024)
#         TREE_DEPTH   nested directory depth for path-resolution phase (default 6)
#         WIDE_FILES   entries in the directory used for the multi-page phases (default 400)
#
set -euo pipefail

MOUNT_POINT=${1:-/mnt/exofs0}
OUTPUT_DIR=${OUTPUT_DIR:-/tmp/output/inventory}
SMALL_FILES=${SMALL_FILES:-64}
SMALL_SIZE=${SMALL_SIZE:-512}
LARGE_SIZE=${LARGE_SIZE:-1024}
TREE_DEPTH=${TREE_DEPTH:-6}
WIDE_FILES=${WIDE_FILES:-400}

TRACE_DIR=/sys/kernel/debug/tracing
WORK="$MOUNT_POINT/inventory"

if [[ $EUID -ne 0 ]]; then
    echo "ERROR must run as root (tracing and mount access)" >&2
    exit 1
fi

if ! mountpoint -q "$MOUNT_POINT"; then
    echo "ERROR $MOUNT_POINT is not mounted" >&2
    exit 1
fi

if [[ ! -d $TRACE_DIR ]]; then
    echo "ERROR $TRACE_DIR missing; mount debugfs first: mount -t debugfs none /sys/kernel/debug" >&2
    exit 1
fi

# --- phase timing -----------------------------------------------------------
# What this time is, and what it is not.
#
# It is NOT device latency. The simulator advances a virtual clock: wait_usec()
# returns immediately and gettimeofday() is read once at init, so nothing in the
# measured path waits for simulated flash.
#
# It IS the host CPU cost of issuing the commands. Every KV command pays a
# blkdev_get_by_path(), a kzalloc of the whole object, a synchronous submission,
# a TCG-emulated MMIO round trip into QEMU, and an osd_read()/osd_write()
# against the backstore. A cache removes commands, so it removes all of that.
# Read these numbers as "host cost of talking to the device", never as
# "how fast the SSD is" -- and always beside the KV command counts, which are
# the load-bearing metric.
PHASE_CSV="$OUTPUT_DIR/phases.csv"
_phase=""
_phase_start=0

phase_close() {
    [[ -n $_phase ]] || return 0
    printf '%s,%s\n' "$_phase" "$(( ($(date +%s%N) - _phase_start) / 1000000 ))" \
        >> "$PHASE_CSV"
    _phase=""
}

mark() {
    # Delimit a phase inside the trace stream, and time the one just ended.
    phase_close
    _phase=$1
    _phase_start=$(date +%s%N)
    echo "PHASE=$1" > "$TRACE_DIR/trace_marker"
}

# Reads of data that never reached the device can block indefinitely, and a
# stalled phase would cost the whole run. Bound them: a timeout is itself a
# result worth recording, not a reason to lose the trace collected so far.
READ_TIMEOUT=${READ_TIMEOUT:-60}
guard() {
    local what=$1; shift
    if ! timeout "$READ_TIMEOUT" "$@" > /dev/null 2>&1; then
        echo "WARNING '$what' did not complete within ${READ_TIMEOUT}s (rc=$?)"
    fi
}

echo "> Resetting ftrace buffer..."
echo 0    > "$TRACE_DIR/tracing_on"
echo nop  > "$TRACE_DIR/current_tracer"
# trace_printk records are small but numerous; give them room.
echo 8192 > "$TRACE_DIR/buffer_size_kb" 2>/dev/null || \
    echo "WARNING could not raise buffer_size_kb; trace may wrap"
echo      > "$TRACE_DIR/trace"
echo 1    > "$TRACE_DIR/tracing_on"

mkdir -p "$OUTPUT_DIR"
printf 'phase,duration_ms\n' > "$PHASE_CSV"
rm -rf "$WORK"

# --- phase 1: directory creation -------------------------------------------
# Creates one directory object plus its attribute object.
mark mkdir_root
mkdir -p "$WORK"
sync

# --- phase 2: small file creation ------------------------------------------
# Each file is a per-inode data object + attribute object; the parent directory
# object is rewritten on every add_link.
mark create_small
i=0
while [[ $i -lt $SMALL_FILES ]]; do
    dd if=/dev/zero of="$WORK/small_$i" bs="$SMALL_SIZE" count=1 2>/dev/null
    i=$((i + 1))
done
sync

# --- phase 3: stat loop ----------------------------------------------------
# The attribute-read path in isolation: one getattr per file, no directory data.
mark stat_loop
i=0
while [[ $i -lt $SMALL_FILES ]]; do
    stat "$WORK/small_$i" > /dev/null
    i=$((i + 1))
done

# --- phase 4: readdir loop -------------------------------------------------
# Whole-directory-object reads; exofs re-reads the object per readdir.
mark readdir_loop
for _ in 1 2 3; do
    guard "readdir" ls -1 "$WORK"
done

# --- phase 5: nested tree + deep path resolution ---------------------------
# Each path component is a lookup: one attribute read + one whole-object read.
mark nested_tree
deep="$WORK/tree"
mkdir -p "$deep"
d=0
while [[ $d -lt $TREE_DEPTH ]]; do
    deep="$deep/level$d"
    mkdir -p "$deep"
    d=$((d + 1))
done
echo leaf > "$deep/leaf.txt"
sync

mark deep_lookup
for _ in 1 2 3; do
    guard "deep lookup" cat "$deep/leaf.txt"
done

# --- phase 5b: a directory bigger than one page ----------------------------
# The phases above all work in directories whose object fits in a single page,
# which hides how a cold read of a larger one behaves: the pages are faulted one
# at a time and every miss fetches the object from byte zero again. WIDE_FILES
# entries put the object over several pages.
mark wide_dir_create
wide="$WORK/wide"
mkdir -p "$wide"
i=0
while [[ $i -lt $WIDE_FILES ]]; do
    : > "$wide/entry_$i"
    i=$((i + 1))
done
sync

mark wide_dir_read_cold
sync; echo 3 > /proc/sys/vm/drop_caches 2>/dev/null || true
guard "wide readdir cold" ls -1 "$wide"

mark wide_dir_read_warm
guard "wide readdir warm" ls -1 "$wide"

mark wide_dir_lookup
sync; echo 3 > /proc/sys/vm/drop_caches 2>/dev/null || true
guard "wide lookup" stat "$wide/entry_$((WIDE_FILES - 1))"

# --- phase 6: large file write + read back ---------------------------------
# Whole-object read amplification: every readpage pulls the entire object.
mark large_write
dd if=/dev/zero of="$WORK/large.bin" bs=1024 count="$LARGE_SIZE" 2>/dev/null
sync

mark large_read
# Drop caches so the read reaches the device rather than the page cache.
sync; echo 3 > /proc/sys/vm/drop_caches 2>/dev/null || true
guard "large read after drop_caches" cat "$WORK/large.bin"

# --- inode map -------------------------------------------------------------
# Object id maps to a type only via the inode: obj_id = ino + EXOFS_OBJ_OFF.
# Capture it before the delete pass removes the evidence.
mark inode_map
find "$WORK" -printf '%i %y %s %p\n' > "$OUTPUT_DIR/inode_map.txt" 2>/dev/null || {
    echo "WARNING find -printf unavailable; falling back to stat"
    find "$WORK" | while read -r p; do
        stat -c '%i %F %s %n' "$p"
    done > "$OUTPUT_DIR/inode_map.txt"
}

# --- phase 7: delete pass --------------------------------------------------
mark delete_pass
rm -rf "$WORK"
sync

phase_close
echo 0 > "$TRACE_DIR/tracing_on"
cp "$TRACE_DIR/trace" "$OUTPUT_DIR/trace.txt"

echo "> Phase timings (host CPU cost of issuing commands, ms):"
column -s, -t < "$PHASE_CSV" 2>/dev/null || cat "$PHASE_CSV"

records=$(grep -c "exofs_kv " "$OUTPUT_DIR/trace.txt" || true)
echo "> Wrote $OUTPUT_DIR/trace.txt ($records KV records) and inode_map.txt"

if [[ "$records" -eq 0 ]]; then
    echo "ERROR no exofs_kv records — is the kernel built with CONFIG_EXOFS_KV_TRACE=y?" >&2
    exit 1
fi
