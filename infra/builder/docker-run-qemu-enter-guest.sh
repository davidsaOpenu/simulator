#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

# Make a fresh copy
evssim_qemu_fresh_image "$version"

# Run qemu detached
evssim_qemu_detached "$version"

# Wait for the VM to boot and expose its NVMe device before entering it
# (QEMU's user-net hostfwd accepts the SSH connection immediately, so without
# this evssim_guest would run before the guest is up and exit right away).
evssim_wait_for_guest
evssim_wait_for_device /dev/nvme0n1

# Run ssh inside
evssim_guest bash
