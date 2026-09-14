#!/bin/bash
set -e

source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/infra/mkfs.exofs" "make -j `nproc` mkfs.exofs"
