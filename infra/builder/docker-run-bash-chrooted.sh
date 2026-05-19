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

# Run bash
evssim_run_chrooted bash
