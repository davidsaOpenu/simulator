#!/bin/bash
source ./builder.sh

test_directory_base="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/tests/$(date +'%Y-%m-%d-%H-%M-%S')"

# Iterate strategies
for strategy in {1..2}; do
    # Make a fresh copy
    evssim_qemu_fresh_image

    # Run qemu with storage sector strategy (Default)
    echo INFO Running test with strategy $strategy
    export EVSSIM_RUNTIME_STORAGE_STRATEGY=$strategy
    evssim_qemu_detached

    # Run tests inside the guest
    set +e
    evssim_guest "cd ./guest; sudo EVSSIM_NEW_BUILDER_GUEST=1 ./run_all_guest_tests.sh"
    test_rc=$?
    set -e

    # Stop qemu and wait
    evssim_qemu_stop

    # Create test directory and gather results
    test_directory=$test_directory_base-strategy-$strategy
    evssim_run_mounted "mkdir -p $test_directory && \
        rsync -qrt --ignore-missing-args ./guest/guest_tests_results.xml $test_directory/pytest-results.xml && \
        rsync -qrt --ignore-missing-args ./guest/fio_tests/reference_results ./guest/Logs $test_directory/"

    # Fail if tests fails
    if [ $test_rc -ne 0 ]; then
        echo "ERROR Guest test failed with strategy=$strategy and error=$test_rc"
        exit $test_rc
    fi
done