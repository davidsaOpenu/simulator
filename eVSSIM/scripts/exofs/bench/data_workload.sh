#!/bin/bash
#
# exofs file-data benchmark.
#
# The companion to object_inventory_workload.sh: that one drives metadata
# (lookup, readdir, stat, attributes), this one drives file contents. Together
# they replace YABS, whose only output is wall-clock throughput: the simulator's
# clock is virtual, so that measures the emulator and the host, not the device.
#
# Emits the same two artefacts as the metadata workload, so parse_kv_trace.py
# reads it unchanged:
#   trace.txt    one ftrace line per NVMe Key-Value command, phase-delimited
#   phases.csv   host CPU cost of each phase in milliseconds
#
# Sizing note. exofs reads a file by fetching the whole prefix up to the last
# page it wants (read_exec() in inode.c asks for off + need starting at zero),
# so reading a file of n pages transfers O(n^2) bytes. A working set sized like
# a normal disk benchmark's would not finish. The defaults below are chosen to
# show that amplification, not to hide it.
#
# Usage:  sudo ./data_workload.sh [mount_point]
# Env:    OUTPUT_DIR  where to write trace.txt / phases.csv (default /tmp/output/data)
#         FILE_COUNT  small files in the many-files phases (default 32)
#         FILE_KB     size of each of those, in KB (default 128)
#         BIG_KB      size of the single large file, in KB (default 4096)
#         BS_LIST     block sizes to write at (default "4k 64k")
#
set -euo pipefail

MOUNT_POINT=${1:-/mnt/exofs0}
OUTPUT_DIR=${OUTPUT_DIR:-/tmp/output/data}
FILE_COUNT=${FILE_COUNT:-32}
FILE_KB=${FILE_KB:-128}
BIG_KB=${BIG_KB:-4096}
BS_LIST=${BS_LIST:-"4k 64k"}
PHASE_TIMEOUT=${PHASE_TIMEOUT:-600}

TRACE_DIR=/sys/kernel/debug/tracing
WORK="$MOUNT_POINT/data"

[[ $EUID -eq 0 ]]            || { echo "ERROR must run as root" >&2; exit 1; }
mountpoint -q "$MOUNT_POINT" || { echo "ERROR $MOUNT_POINT is not mounted" >&2; exit 1; }
[[ -d $TRACE_DIR ]]          || { echo "ERROR mount debugfs first" >&2; exit 1; }

mkdir -p "$OUTPUT_DIR"
PHASE_CSV="$OUTPUT_DIR/phases.csv"
printf 'phase,duration_ms\n' > "$PHASE_CSV"
_phase=""
_phase_start=0

phase_close() {
    [[ -n $_phase ]] || return 0
    printf '%s,%s\n' "$_phase" "$(( ($(date +%s%N) - _phase_start) / 1000000 ))" \
        >> "$PHASE_CSV"
    _phase=""
}

mark() {
    phase_close
    _phase=$1
    _phase_start=$(date +%s%N)
    echo "PHASE=$1" > "$TRACE_DIR/trace_marker"
}

# A phase that never returns costs the whole run and the trace with it. Bound
# each one: a timeout is a result worth recording.
guard() {
    local what=$1; shift
    timeout "$PHASE_TIMEOUT" "$@" > /dev/null 2>&1 \
        || echo "WARNING '$what' did not complete within ${PHASE_TIMEOUT}s"
}

# A read that quietly returns nothing looks like a very fast read. Every read
# phase goes through here so a short one is reported rather than timed.
short_reads=0
read_file() {
    local path=$1 bs=$2 want got
    want=$(stat -c %s "$path")
    got=$(timeout "$PHASE_TIMEOUT" dd if="$path" bs="$bs" 2>/dev/null | wc -c)
    if [[ "$got" != "$want" ]]; then
        echo "WARNING short read: $path returned $got of $want bytes"
        short_reads=$((short_reads + 1))
    fi
}

# O_DIRECT is not honoured here -- exofs has no ->direct_IO -- so the only way
# to make a read reach the device is to drop the page cache.
cold() { sync; echo 3 > /proc/sys/vm/drop_caches 2>/dev/null || true; }

echo "> Resetting ftrace buffer..."
echo 0    > "$TRACE_DIR/tracing_on"
echo nop  > "$TRACE_DIR/current_tracer"
echo 8192 > "$TRACE_DIR/buffer_size_kb" 2>/dev/null || \
    echo "WARNING could not raise buffer_size_kb; trace may wrap"
echo      > "$TRACE_DIR/trace"
echo 1    > "$TRACE_DIR/tracing_on"

rm -rf "$WORK"
mkdir -p "$WORK"

# numfmt is not on every Trusty image; the block sizes here are a short list.
bs_bytes() {
    case $1 in
        *k|*K) echo $(( ${1%[kK]} * 1024 )) ;;
        *m|*M) echo $(( ${1%[mM]} * 1024 * 1024 )) ;;
        *)     echo "$1" ;;
    esac
}

# --- sequential write, one block size per phase -----------------------------
for bs in $BS_LIST; do
    mark "seq_write_$bs"
    guard "seq write $bs" dd if=/dev/zero of="$WORK/big_$bs" bs="$bs" \
        count=$(( BIG_KB * 1024 / $(bs_bytes "$bs") )) conv=fsync
done

# --- sequential read of the large file, cold --------------------------------
# The prefix-per-batch read makes this the phase that exposes read amplification.
for bs in $BS_LIST; do
    cold
    mark "seq_read_$bs"
    read_file "$WORK/big_$bs" "$bs"
done

# --- rewrite in place -------------------------------------------------------
# Every store used to replace the whole object regardless of offset. With the
# offset honoured this should cost one command for the range touched.
cold
mark rewrite_head
guard "rewrite head" dd if=/dev/urandom of="$WORK/big_4k" bs=4k count=16 \
    conv=notrunc,fsync

# --- many small files -------------------------------------------------------
mark many_write
i=0
while [[ $i -lt $FILE_COUNT ]]; do
    dd if=/dev/zero of="$WORK/f$i" bs=1024 count="$FILE_KB" 2>/dev/null
    i=$((i + 1))
done
sync

cold
mark many_read
i=0
while [[ $i -lt $FILE_COUNT ]]; do
    read_file "$WORK/f$i" 64k
    i=$((i + 1))
done

# --- random read ------------------------------------------------------------
# One 4K read per file at a pseudo-random offset: the worst case for a backend
# that always fetches from byte zero.
cold
mark rand_read
i=0
while [[ $i -lt $FILE_COUNT ]]; do
    guard "rand read f$i" dd if="$WORK/f$i" of=/dev/null bs=4k count=1 \
        skip=$(( (i * 7) % (FILE_KB / 4) ))
    i=$((i + 1))
done

# --- inode map --------------------------------------------------------------
mark inode_map
find "$WORK" -printf '%i %y %s %p\n' > "$OUTPUT_DIR/inode_map.txt" 2>/dev/null || \
    find "$WORK" -exec stat -c '%i %F %s %n' {} \; > "$OUTPUT_DIR/inode_map.txt"

mark delete_pass
rm -rf "$WORK"
sync

phase_close
echo 0 > "$TRACE_DIR/tracing_on"
cp "$TRACE_DIR/trace" "$OUTPUT_DIR/trace.txt"

records=$(grep -c "exofs_kv " "$OUTPUT_DIR/trace.txt" || true)
echo "> Wrote $OUTPUT_DIR/trace.txt ($records KV records) and inode_map.txt"
echo "> Short reads: $short_reads"
echo "> Phase timings (host CPU cost of issuing commands, ms):"
column -s, -t < "$PHASE_CSV" 2>/dev/null || cat "$PHASE_CSV"

if [[ "$records" -eq 0 ]]; then
    echo "ERROR no exofs_kv records - is the kernel built with CONFIG_EXOFS_KV_TRACE=y?" >&2
    exit 1
fi
