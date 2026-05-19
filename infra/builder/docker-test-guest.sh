#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

internal_image_path="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE"
home_path="/home/$EVSSIM_QEMU_UBUNTU_USERNAME"
# EVSSIM_GUEST_ROOT_PATH provides the same path, but is not root??? Is it an error?

guest_test() {
    local output=$1
    local strategy=$2
    local simulator=$3
    local test_name=$4

    test_index=$(($test_index+1))

    # Make a fresh copy
    evssim_qemu_fresh_image "$version"

    # Run qemu with test specific configuration
    EVSSIM_RUNTIME_STORAGE_STRATEGY=$strategy EVSSIM_QEMU_SIMULATOR_ENABLED=$simulator evssim_qemu_detached "$version"

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
    evssim_run "$version" "mkdir -p /tmp/guest-logs \
        && sudo virt-copy-out -a $internal_image_path -i $home_path/guest/Logs /tmp/guest-logs \
	&& mkdir -p $test_directory_base \
	&& mv /tmp/guest-logs/Logs $test_directory"

    # Fail if tests fails
    if [ $test_rc -ne 0 ]; then
        echo "ERROR Guest test failed with strategy=$strategy, test=$test_name, error=$test_rc"
        echo "ERROR See logs @ $test_directory"
        exit $test_rc
    fi
}

# Configure directory base
test_directory_base="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_LOGS_FOLDER/tests/$(date +'%Y-%m-%d-%H-%M-%S')"
test_index=0

# Run disk tests
guest_test "$test_directory_base" 1 yes nvme_compliance_tests
guest_test "$test_directory_base" 1 no fio_tests

# Run simulator specific tests (With different strategies)
guest_test "$test_directory_base" 2 yes objects_via_ioctl
# NOTE This is mock for future tests
#guest_test "$test_directory_base" 1 on simulator_test0
#guest_test "$test_directory_base" 2 on simulator_test0
