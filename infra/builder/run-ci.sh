#!/bin/bash
set -e

# Configure a unique image name for the test
export EVSSIM_DOCKER_IMAGE_NAME=evssim:ci-$(date "+%Y.%m.%d-%H.%M.%S")

# Make sure every run we get a new clear runtime folder
export EVSSIM_RUNTIME_ALWAYS_RESET=yes

# Build images
./build-docker-image.sh
./build-qemu-image.sh

# Compile all modules
./compile-kernel.sh
./compile-qemu.sh
./compile-nvme-tools.sh
./compile-tests.sh

# Run sanity and tests
#./docker-run-sanity.sh
#./docker-test-host.sh
#./docker-test-guest.sh
