#!/bin/bash
set -e
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

# Artifact builds export pinned local objects instead of downloading inside
# configure. Prepare the sources that the ordinary compile path supplied.
if [[ "$version" == ubuntu-26.04 ]]; then
    evssim_run_at_folder "$version" "$EVSSIM_QEMU_FOLDER" \
        python3 "$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/qemu-artifact.py" \
        --prepare-subprojects .
else
    git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" submodule update --init --recursive \
        -- dtc capstone ui/keycodemapdb
fi
