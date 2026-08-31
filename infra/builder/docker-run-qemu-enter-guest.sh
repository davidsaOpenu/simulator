#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

# Make a fresh copy
evssim_qemu_fresh_image "$version"

# Run qemu detached
evssim_qemu_detached "$version"

# Wait for the NVMe device to appear before entering the guest
evssim_wait_for_device /dev/nvme0n1

# Run ssh inside
evssim_guest bash
