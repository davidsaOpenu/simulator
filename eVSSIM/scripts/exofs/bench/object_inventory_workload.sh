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
#
set -euo pipefail

MOUNT_POINT=${1:-/mnt/exofs0}
OUTPUT_DIR=${OUTPUT_DIR:-/tmp/output/inventory}
SMALL_FILES=${SMALL_FILES:-64}
SMALL_SIZE=${SMALL_SIZE:-512}
LARGE_SIZE=${LARGE_SIZE:-1024}
TREE_DEPTH=${TREE_DEPTH:-6}

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

mark() {
    # Delimit a phase inside the trace stream.
    echo "PHASE=$1" > "$TRACE_DIR/trace_marker"
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
    ls -1 "$WORK" > /dev/null
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
    cat "$deep/leaf.txt" > /dev/null
done

# --- phase 6: large file write + read back ---------------------------------
# Whole-object read amplification: every readpage pulls the entire object.
mark large_write
dd if=/dev/zero of="$WORK/large.bin" bs=1024 count="$LARGE_SIZE" 2>/dev/null
sync

mark large_read
# Drop caches so the read reaches the device rather than the page cache.
sync; echo 3 > /proc/sys/vm/drop_caches 2>/dev/null || true
cat "$WORK/large.bin" > /dev/null

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

echo 0 > "$TRACE_DIR/tracing_on"
cp "$TRACE_DIR/trace" "$OUTPUT_DIR/trace.txt"

records=$(grep -c "exofs_kv " "$OUTPUT_DIR/trace.txt" || true)
echo "> Wrote $OUTPUT_DIR/trace.txt ($records KV records) and inode_map.txt"

if [[ "$records" -eq 0 ]]; then
    echo "ERROR no exofs_kv records — is the kernel built with CONFIG_EXOFS_KV_TRACE=y?" >&2
    exit 1
fi
