#!/bin/bash
source ./builder.sh

exofs_test() {
    # Make a fresh copy
    evssim_qemu_fresh_image

    # Run qemu with test specific configuration
    EVSSIM_RUNTIME_STORAGE_STRATEGY=2 EVSSIM_QEMU_SIMULATOR_ENABLED=yes evssim_qemu_detached

    # Run tests inside the guest
    echo "INFO Running exofs test"
    OUTPUT_DIR="/tmp/output"
    set +e
    evssim_guest "sudo OUTPUT_DIR=$OUTPUT_DIR ./exofs/run_osd_emulator_and_mount_exofs.sh"
    test_rc=$?
    set -e

    # then debugging you can find trace logs at OUTPUT_DIR
    evssim_guest_copy $OUTPUT_DIR $OUTPUT_DIR 
    # Stop qemu and wait
    evssim_qemu_flush_disk
    evssim_qemu_stop

    # Fail if tests fails
    if [ $test_rc -ne 0 ]; then
        echo "ERROR exofs test failed"
        exit $test_rc
    fi
}

exofs_test "$@"
