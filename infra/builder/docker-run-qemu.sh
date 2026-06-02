#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

# Make a fresh copy
# ubuntu-14.04 won't work - libguestfs too old for new images.
evssim_qemu_fresh_image ubuntu-26.04

# Run qemu
evssim_qemu_attached "$version"
