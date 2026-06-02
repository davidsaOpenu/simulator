#!/bin/bash
source ./builder.sh
version="${EVSSIM_HOST_TESTS_RUN_CONTAINER#ubuntu:}"
export EVSSIM_DOCKER_IMAGE_NAME="$EVSSIM_DOCKER_IMAGE_NAME:$version"
evssim_run_at_folder $EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host ./run_all_host_tests.sh
