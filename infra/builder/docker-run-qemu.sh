#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

# Make a fresh copy
evssim_qemu_fresh_image "$EVSSIM_GUESTFS_DOCKER_IMAGE"

# Run qemu
evssim_qemu_attached "$version"
