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

    test_index=$(($test_index+1))

    # Make a fresh copy
    evssim_qemu_fresh_image "$version" core

    # Run qemu with test specific configuration
    EVSSIM_RUNTIME_STORAGE_STRATEGY=$strategy EVSSIM_QEMU_SIMULATOR_ENABLED=$simulator evssim_qemu_detached "$version"

    # Wait for the NVMe device to appear before running tests.
    evssim_wait_for_device /dev/nvme0n1

    # Run tests inside the guest
    echo "INFO Running test id=$test_index strategy=$strategy simulator=$simulator test=$test_name"
    set +e
    # fio changes its working directory; keep the XML where the collector reads it.
    evssim_guest "cd ./guest; mkdir Logs; sudo VSSIM_NEXTGEN_BUILD_SYSTEM=1 EVSSIM_PROVISIONED_DEVICE_COUNT=$EVSSIM_PROVISIONED_DEVICE_COUNT nosetests3 -v -s --with-xunit --xunit-file=$EVSSIM_GUEST_HOME_PATH/guest/guest_tests_results.xml $test_name"
    test_rc=$?
    set -e

    local collection_rc=0 flush_rc=0 stop_rc=0
    if [ -n "${EVSSIM_WORKFLOW_STAGE_DIR:-}" ]; then
        # Copy JUnit and raw measurements into Logs before this image is replaced.
        evssim_guest "cd ./guest; sudo bash ./common/collect-workflow-evidence.sh $test_name" || collection_rc=$?
    fi

    # Collect evidence and attempt shutdown even when the test failed. Preserve
    # the test status if a later collection or shutdown command also fails.
    evssim_qemu_flush_disk || flush_rc=$?
    evssim_qemu_stop || stop_rc=$?

    # Create test directory and gather results
    test_directory=$test_directory_base/run-$test_index-strategy-$strategy-test-$test_name
    if [ "$stop_rc" -eq 0 ]; then
        # virt-copy-out preserves root-owned directories. Moving to the
        # workspace can cross filesystems and must also remove that source tree.
        # Give the extracted tree to the build user for the next checkout cleanup.
        evssim_run "$EVSSIM_GUESTFS_DOCKER_IMAGE" "mkdir -p /tmp/guest-logs \
        && sudo virt-copy-out -a $internal_image_path -i $EVSSIM_GUEST_HOME_PATH/guest/Logs /tmp/guest-logs \
	&& mkdir -p $test_directory_base \
	&& sudo mv /tmp/guest-logs/Logs $test_directory \
        && sudo chown -R $EVSSIM_EXTERNAL_UID:$EVSSIM_EXTERNAL_GID $test_directory" || collection_rc=$?
        if [ -n "${EVSSIM_WORKFLOW_STAGE_DIR:-}" ]; then
            local collected="$EVSSIM_ROOT_PATH/${test_directory#"$EVSSIM_DOCKER_ROOT_PATH/"}"
            if ! mkdir -p "$EVSSIM_WORKFLOW_STAGE_DIR/guest/$test_index" || \
                    ! cp -a "$collected/workflow/." "$EVSSIM_WORKFLOW_STAGE_DIR/guest/$test_index/"; then
                if [ "$collection_rc" -eq 0 ]; then collection_rc=1; fi
            fi
        fi
    fi

    # Fail if tests fails
    echo "INFO Guest results: test=$test_rc collection=$collection_rc flush=$flush_rc stop=$stop_rc"
    local result
    for result in "$test_rc" "$collection_rc" "$flush_rc" "$stop_rc"; do
        if [ "$result" -ne 0 ]; then
            echo "ERROR Guest stage failed with strategy=$strategy, test=$test_name; logs @ $test_directory"
            return "$result"
        fi
    done
}

# Configure directory base
test_directory_base="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_LOGS_FOLDER/tests/$(date +'%Y-%m-%d-%H-%M-%S')"
test_index=0

# Disabled tests (temporarily) for kernel 7.0.2:
# * nvme_compliance_tests - Require dnvme+tnvme which hasn't been ported to
# the new kernel yet, and as such has been disabled until dnvme is updated.
# * objects_via_ioctl - Require custom nvme-cli object-storage subcommands
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
fi

# NOTE This is mock for future tests
#guest_test "$test_directory_base" 1 on simulator_test0
#guest_test "$test_directory_base" 2 on simulator_test0
