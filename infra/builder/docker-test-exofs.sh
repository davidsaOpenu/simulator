#!/bin/bash
source ./builder.sh

exofs_test() {
    # Make a fresh copy
    evssim_qemu_fresh_image

    # Run qemu with test specific configuration
    EVSSIM_RUNTIME_STORAGE_STRATEGY=2 EVSSIM_QEMU_SIMULATOR_ENABLED=yes evssim_qemu_detached

    # Run tests inside the guest
    echo "INFO Running exofs test"
    set +e
    evssim_guest "sudo ./run_osd_emulator_and_mount_exofs.sh"
    test_rc=$?
    set -e

    # Stop qemu and wait
    evssim_qemu_flush_disk
    evssim_qemu_stop

    # Fail if tests fails
    if [ $test_rc -ne 0 ]; then
        echo "ERROR exofs test failed"
        exit $test_rc
    fi
}

exofs_test
