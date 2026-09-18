#!/bin/bash
#
# Run fio and report its rates in simulated device time.
#
# fio times itself with the guest's clock, which under emulation follows the
# host, so its bandwidth and latency measure TCG and the host CPU rather than
# the modelled SSD. The simulator keeps its own clock, advanced only by the
# modelled delay of each flash operation, and QEMU returns it in vendor log
# page 0xc0. This reads that clock before and after the run and divides fio's
# own byte and I/O counts by the difference. fio's output is printed unchanged
# above the result.
#
# Everything the device does between the two readings is charged to the run:
#  - fio laying out its files. By default it preallocates with posix_fallocate(),
#    which exofs does not implement, so glibc writes every block instead and a
#    write run costs twice what its own writes do. Pass --fallocate=none. A read
#    run lays out a missing file by writing it; create files in an earlier run.
#  - background garbage collection, and any other simulated device's work. The
#    clock is shared by every simulated device, so run one workload at a time.
# Reads served from the page cache cost no device time. fio's default
# invalidate=1 drops the file's pages before the job starts.
#
# Works on any filesystem or raw device backed by a simulated NVMe device.
#
# Usage:  sudo ./fio_sim_time.sh <fio arguments or job file>
#   e.g.  sudo ./fio_sim_time.sh --name=seqwrite --directory=/mnt/exofs0 \
#             --rw=write --bs=4k --size=4m --ioengine=sync --fallocate=none
# Env:    NVME_CTRL  controller to read the clock from (default /dev/nvme0)
#         NVME       nvme-cli binary (default /home/esd/guest/nvme)
#
set -euo pipefail

NVME_CTRL=${NVME_CTRL:-/dev/nvme0}
NVME=${NVME:-/home/esd/guest/nvme}

[[ $EUID -eq 0 ]] || { echo "ERROR must run as root" >&2; exit 1; }
[[ $# -gt 0 ]]    || { echo "usage: $0 <fio arguments or job file>" >&2; exit 1; }

# The simulator clock in microseconds, from vendor log page 0xc0 (192).
device_clock_us() {
    local raw
    raw=$("$NVME" get-log "$NVME_CTRL" --log-id=192 --log-len=8 --raw-binary 2>/dev/null |
          od -An -t d8) || return 1
    raw=${raw//[[:space:]]/}
    [[ $raw =~ ^[0-9]+$ ]] || return 1
    echo "$raw"
}

# Anything still dirty belongs to whatever ran before, not to this run.
sync
start=$(device_clock_us) || {
    echo "ERROR $NVME_CTRL did not return the simulator clock (NVMe log page 0xc0)." >&2
    echo "      This QEMU does not have the change that adds it." >&2
    exit 1
}

out=$(mktemp)
trap 'rm -f "$out"' EXIT
fio "$@" | tee "$out"
sync
end=$(device_clock_us)

# fio 2.1's normal output is the only one carrying exact I/O counts, on its
# "issued" line; bytes come from the "io=" field of each direction's line.
awk -v us=$((end - start)) '
function bytes(s,   n, u) {
    n = s + 0
    u = s; sub(/^[0-9.]+/, "", u)
    if (u ~ /^K/) return n * 1024
    if (u ~ /^M/) return n * 1024 * 1024
    if (u ~ /^G/) return n * 1024 * 1024 * 1024
    if (u ~ /^P/) return n * 1024 * 1024 * 1024 * 1024
    return n
}
/^ +(read |write|mixed): io=/ {
    v = $0; sub(/.*io=/, "", v); sub(/,.*/, "", v)
    if ($1 ~ /^write/) wb += bytes(v); else rb += bytes(v)
}
/issued *: total=r=/ {
    v = $0; sub(/.*total=r=/, "", v); split(v, a, /[\/,=]/)
    rios += a[1]; wios += a[3]
}
END {
    ios = rios + wios
    printf "\nSimulated device time: %.6f s (NVMe log page 0xc0)\n", us / 1e6
    printf "  read : %.1f KiB in %d I/Os\n", rb / 1024, rios
    printf "  write: %.1f KiB in %d I/Os\n", wb / 1024, wios
    if (us <= 0) {
        print "  The device did no work: every I/O was served from the page cache."
        exit
    }
    printf "  in device time: %.3f MiB/s, %.1f IOPS", (rb + wb) / 1048576 / (us / 1e6), ios / (us / 1e6)
    if (ios > 0) printf ", %.1f us per I/O", us / ios
    printf "\n"
}' "$out"
