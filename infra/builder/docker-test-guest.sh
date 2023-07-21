#!/bin/bash
source ./builder.sh

guest_test() {
    local output=$1
    local strategy=$2
    local simulator=$3
    local test_name=$4

    test_index=$(($test_index+1))

    # Make a fresh copy
    evssim_qemu_fresh_image

    # Run qemu with test specific configuration
    EVSSIM_RUNTIME_STORAGE_STRATEGY=$strategy EVSSIM_QEMU_SIMULATOR_ENABLED=$simulator evssim_qemu_detached

    # Run tests inside the guest
    echo "INFO Running test id=$test_index strategy=$strategy simulator=$simulator test=$test_name"
    set +e
    evssim_guest "cd ./guest; mkdir Logs; sudo VSSIM_NEXTGEN_BUILD_SYSTEM=1 nosetests -v --with-xunit --xunit-file=guest_tests_results.xml $test_name"
    test_rc=$?
    set -e

    # Stop qemu and wait
    evssim_qemu_flush_disk
    evssim_qemu_stop

    # Create test directory and gather results
    test_directory=$test_directory_base/run-$test_index-strategy-$strategy-test-$test_name
    evssim_run_mounted "mkdir -p $test_directory && rsync -qrt --ignore-missing-args ./guest/Logs $test_directory/"

    # Fail if tests fails
    if [ $test_rc -ne 0 ]; then
        echo "ERROR Guest test failed with strategy=$strategy, test=$test_name, error=$test_rc"
        echo "ERROR See logs @ $test_directory"
        exit $test_rc
    fi
}

# Configure directory base
test_directory_base="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_LOGS_FOLDER/tests/$(date +'%Y-%m-%d-%H-%M-%S')"

# Run disk tests
guest_test "$test_directory_base" 1 on nvme_compliance_tests
guest_test "$test_directory_base" 1 on fio_tests

# Run simulator specific tests (With different strategies)
guest_test "$test_directory_base" 1 on objects_via_ioctl
# NOTE This is mock for future tests
#guest_test "$test_directory_base" 1 on simulator_test0
#guest_test "$test_directory_base" 2 on simulator_test0
