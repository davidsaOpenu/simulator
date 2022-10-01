#!/bin/bash
set -e

source ./elk.sh

# Configure a unique image name for the test
export EVSSIM_DOCKER_IMAGE_NAME=evssim:ci-latest #$(date "+%Y.%m.%d-%H.%M.%S")

# Make sure every run we get a new clear runtime folder
export EVSSIM_RUNTIME_ALWAYS_RESET=yes

# Build images
./build-docker-image.sh
./build-qemu-image.sh

# Compile all modules
./compile-kernel.sh
./compile-qemu.sh
./compile-host-tests.sh
./compile-guest-tests.sh

# Run sanity and tests
./docker-run-sanity.sh

trap evssim_stop_elk_stack EXIT
evssim_run_elk_stack

./docker-test-host.sh
./docker-test-guest.sh