#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

# Make a fresh copy
# ubuntu-14.04 won't work - libguestfs too old for new images.
evssim_qemu_fresh_image ubuntu-26.04

# Run qemu detached
evssim_qemu_detached "$version"

# Wait for the NVMe device to appear before entering the guest
evssim_wait_for_device /dev/nvme0n1

# Run ssh inside
evssim_guest bash
