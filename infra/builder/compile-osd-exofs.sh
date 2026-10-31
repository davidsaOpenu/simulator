#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

# Keep upstream's standalone entry point, with S4's clean build and input record.
# The configured 14.04 tool builder also serves QEMU 11 without compiling QEMU.
exec "$EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/compile-exofs-tools.sh" "$version"
