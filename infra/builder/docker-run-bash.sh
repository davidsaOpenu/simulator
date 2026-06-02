#!/bin/bash
source ./builder.sh

prog=$(basename $0)
if [ "$#" -gt 1 ]; then
	echo $prog: Error: Too many arguments.
	exit 2
fi
version="${1:-14.04}"

export EVSSIM_DOCKER_IMAGE_NAME="$EVSSIM_DOCKER_IMAGE_NAME:$version"

# Run bash
evssim_run bash
