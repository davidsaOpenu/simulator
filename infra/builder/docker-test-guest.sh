#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"
kernel_branch=$(git -C "$EVSSIM_KERNEL_FOLDER" branch --show-current)

internal_image_path="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE"

guest_test() {
    local output=$1
    local strategy=$2
    local simulator=$3
    local test_name=$4
    local required_events=${5:-}

    test_index=$(($test_index+1))

    # Make a fresh copy
    evssim_qemu_fresh_image

    # Run qemu with test specific configuration
    EVSSIM_QEMU_TRACE_EVENTS="$required_events" EVSSIM_RUNTIME_STORAGE_STRATEGY=$strategy EVSSIM_QEMU_SIMULATOR_ENABLED=$simulator evssim_qemu_detached "$version"

    # Wait for the NVMe device to appear before running tests.
    evssim_wait_for_device /dev/nvme0n1

    # Run tests inside the guest
    echo "INFO Running test id=$test_index strategy=$strategy simulator=$simulator test=$test_name"
    set +e
    evssim_guest "cd ./guest; mkdir Logs; sudo VSSIM_NEXTGEN_BUILD_SYSTEM=1 EVSSIM_PROVISIONED_DEVICE_COUNT=$EVSSIM_PROVISIONED_DEVICE_COUNT nosetests3 -v -s --with-xunit --xunit-file=guest_tests_results.xml $test_name"
    test_rc=$?
    set -e

    # Fail if a required trace event never fired; QEMU's log is lost once it stops
    for event in $required_events; do
        count=$(docker logs "$EVSSIM_DOCKER_UUID" 2>&1 | grep -c ":$event " || true)
        echo "INFO $event fired $count times"
        if [ "$count" -eq 0 ]; then
            echo "ERROR Required QEMU trace event $event never fired"
            test_rc=1
        fi
    done

    # Stop qemu and wait
    evssim_qemu_flush_disk
    evssim_qemu_stop

    # Create test directory and gather results
    # ubuntu-14.04 won't work - libguestfs too old for new images.
    test_directory=$test_directory_base/run-$test_index-strategy-$strategy-test-$test_name
    evssim_run "$EVSSIM_GUESTFS_DOCKER_IMAGE" "mkdir -p /tmp/guest-logs \
        && sudo virt-copy-out -a $internal_image_path -i $EVSSIM_GUEST_HOME_PATH/guest/Logs /tmp/guest-logs \
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

# Disabled tests (temporarily) for kernel 7.0.2:
# * nvme_compliance_tests - Require dnvme+tnvme which hasn't been ported to
# the new kernel yet, and as such has been disabled until dnvme is updated.
# * objects_via_ioctl, objects_queue_reset - Require custom nvme-cli object-storage subcommands
# (objw, objr, objd, objl, obje) that were added to the 'master' nvme-cli
# branch and needs to be implemented in the new '3.0-a.5' branch.

# `basic_disk_tests` are disabled for 5.0.0 branch because old nvme-cli
# has slightly different subcommands, and it's generally not needed when
# nvme_compliance_tests and objects_via_ioctl tests are available.

# Run disk tests
if [ "$kernel_branch" != "master" ]; then
    guest_test "$test_directory_base" 1 yes basic_disk_tests
fi
if [ "$kernel_branch" = "master" ]; then
    guest_test "$test_directory_base" 1 yes nvme_compliance_tests
fi
guest_test "$test_directory_base" 1 no fio_tests
guest_test "$test_directory_base" 1 yes ext4

# Run simulator specific tests (With different strategies)
if [ "$kernel_branch" = "master" ]; then
    guest_test "$test_directory_base" 2 yes objects_via_ioctl
    guest_test "$test_directory_base" 2 yes objects_queue_reset nvme_del_sq
    # TODO: once the object path pauses, run with REALTIME_DELAY on and also require
    # nvme_drain_kv (a queue deleted with object commands in flight); until then no
    # object command is ever in flight, so it never fires
    #guest_test "$test_directory_base" 2 yes objects_queue_reset "nvme_del_sq nvme_drain_kv"
fi

# NOTE This is mock for future tests
#guest_test "$test_directory_base" 1 on simulator_test0
#guest_test "$test_directory_base" 2 on simulator_test0
