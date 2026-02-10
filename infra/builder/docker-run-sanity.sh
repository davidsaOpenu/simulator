#!/bin/bash
source ./builder.sh

# Make a fresh copy
evssim_qemu_fresh_image

# Run qemu
evssim_qemu_detached

# Wait for NVMe devices to appear in the guest (retry for up to 60s)
# The guest kernel needs time after SSH becomes available to probe all NVMe controllers
echo "Waiting for NVMe devices to appear in guest..."
for attempt in $(seq 1 30); do
    if evssim_guest ls /dev/nvme0n1 2>/dev/null >/dev/null; then
        echo "NVMe devices ready after $((attempt * 2))s"
        break
    fi
    sleep 2
done

# Verify all 6 NVMe controllers are detected
echo "Verifying all NVMe controllers..."
for i in $(seq 0 5); do
    if ! evssim_guest test -e /dev/nvme${i}n1 2>/dev/null; then
        echo "ERROR: Missing /dev/nvme${i}n1"
        exit 1
    fi
done
echo "All 6 NVMe controllers detected successfully"

# Final sanity checks
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