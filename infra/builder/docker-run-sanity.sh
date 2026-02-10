#!/bin/bash
source ./builder.sh

# Make a fresh copy
evssim_qemu_fresh_image

# Run qemu
evssim_qemu_detached


# evssim_guest ls -al /dev/nvme0n1 2>/dev/null >/dev/null
# Before checking if device nvme0n1 exists we should wait
# Otherwise there are sporadic failures on "Failed to find device" error
sleep 10

# Verify the QEMU container is still running before attempting SSH
if [ ! -z "$EVSSIM_DOCKER_UUID" ] && ! docker ps -q --no-trunc | grep -q "$EVSSIM_DOCKER_UUID"; then
    echo "ERROR: QEMU container exited before guest was ready."
    echo "ERROR: Docker logs follow:"
    docker logs "$EVSSIM_DOCKER_UUID" 2>&1 || echo "(container already removed)"
    exit 1
fi

# Run a command inside the container (check if device nvme0n1 exists)
if evssim_guest ls -al /dev/nvme0n1 2>/dev/null >/dev/null; then
    echo "eVSSIM Up & Running!"
else
    echo "eVSSIM Failed to find /dev/nvme0n1."
    echo "ERROR: Docker logs follow:"
    docker logs "$EVSSIM_DOCKER_UUID" 2>&1 || echo "(container already removed)"
    exit 1
fi
# Check with multiple disks
if evssim_guest ls -al /dev/nvme1n1 2>/dev/null >/dev/null; then
    echo "eVSSIM Up & Running!"
else
    echo "eVSSIM Failed to find /dev/nvme1n1."
    echo "ERROR: Docker logs follow:"
    docker logs "$EVSSIM_DOCKER_UUID" 2>&1 || echo "(container already removed)"
    exit 1
fi
