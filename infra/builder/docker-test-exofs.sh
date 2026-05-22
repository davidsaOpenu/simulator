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
    local benchmark_toggle=${EVSSIM_RUN_FS_BENCHMARKS:-yes}
    local benchmark_cmd=""
    local yabs_dir=${YABS_DIR:-/home/esd/yet-another-bench-script}
    set +e
    evssim_guest "sudo env OUTPUT_DIR=$OUTPUT_DIR ./exofs/run_osd_emulator_and_mount_exofs.sh"
    test_rc=$?
    if [ $test_rc -eq 0 ]; then
        printf -v benchmark_cmd 'cd ./guest && sudo env OUTPUT_DIR=%q EVSSIM_RUN_FS_BENCHMARKS=%q YABS_DIR=%q nosetests -v run_fs_benchmarks' "$OUTPUT_DIR" "$benchmark_toggle" "$yabs_dir"
        evssim_guest "$benchmark_cmd"
        test_rc=$?
    fi
    set -e

    # When debugging you can find trace logs at OUTPUT_DIR
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
