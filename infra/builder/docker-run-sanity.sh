#!/bin/bash
source ./builder.sh

prog=$(basename "$0")
if [ "$#" -ne 1 ]; then
    echo $prog: Error: Wrong number of arguments.
    echo "Usage: $prog CONTAINER_VERSION"
    exit 2
fi
version="$1"

export EVSSIM_DOCKER_IMAGE_NAME="$EVSSIM_DOCKER_IMAGE_NAME:$version"

# Make a fresh copy
evssim_qemu_fresh_image

# Run qemu
evssim_qemu_detached


# evssim_guest ls -al /dev/nvme0n1 2>/dev/null >/dev/null
# Before checking if device nvme0n1 exists we should wait
# Otherwise there are sporadic failures on "Failed to find device" error
# sleep 10

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
