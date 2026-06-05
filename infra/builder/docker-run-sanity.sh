#!/bin/bash
source ./builder.sh

wait_for_evssim_device () {
    local device=${1:-/dev/nvme0n1}
    local max_wait=${2:-60}
    local interval=2
    local elapsed=0
    echo "INFO Waiting for NVMe device $device (up to ${max_wait}s)..."
    while ! evssim_guest ls -al "$device" 2>/dev/null >/dev/null; do
        elapsed=$((elapsed + interval))
        if [ $elapsed -ge $max_wait ]; then
            echo "ERROR NVMe device $device not available after ${max_wait}s"
            return 1
        fi
        sleep $interval
    done
    echo "INFO NVMe device $device ready after ${elapsed}s"
}

# Make a fresh copy
evssim_qemu_fresh_image

# Run qemu
evssim_qemu_detached

evssim_wait_for_guest
wait_for_evssim_device /dev/nvme0n1

# Run a command inside the container (check if device nvme0n1 exists)
if evssim_guest ls -al /dev/nvme0n1 2>/dev/null >/dev/null; then
    echo "eVSSIM Up & Running!"
else
    echo "eVSSIM Failed to find /dev/nvme0n1."
    exit 1
fi
# Check with multiple disks
if evssim_guest ls -al /dev/nvme1n1 2>/dev/null >/dev/null; then
    echo "eVSSIM Up & Running!"
else
    echo "eVSSIM Failed to find /dev/nvme1n1."
    exit 1
fi
