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
evssim_elk_build_test_image

# Compile all modules
./compile-kernel.sh
./compile-qemu.sh
./compile-host-tests.sh
./compile-guest-tests.sh

# Run sanity and tests
./docker-run-sanity.sh

./docker-test-host.sh
./docker-test-guest.sh


# ELK stack tests
trap evssim_elk_stop_stack EXIT

rm -rf ${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}/*
cp -a $EVSSIM_ROOT_PATH/$EVSSIM_ELK_FOLDER/sample/* ${EVSSIM_ROOT_PATH}/${EVSSIM_LOGS_FOLDER}

evssim_elk_run_stack
evssim_elk_run_tests
