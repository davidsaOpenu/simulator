#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

filesystems_test() {
    # Make a fresh copy
    evssim_qemu_fresh_image "$version"

    # FTL events logged after this marker belong to this run
    local marker
    marker=$(mktemp)

    # Object mode stores its data under /tmp/osd, not in the device's RAM, so
    # back it with tmpfs too
    EVSSIM_DOCKER_XOPTIONS="$EVSSIM_DOCKER_XOPTIONS --tmpfs /tmp/osd" \
    EVSSIM_RUNTIME_SSD_CONF_TEMPLATE="$EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker/ssd.conf.filesystems.template" \
    EVSSIM_RUNTIME_ALWAYS_RESET=yes \
    EVSSIM_QEMU_SIMULATOR_ENABLED=yes \
    EVSSIM_QEMU_MEMORY=512M \
        evssim_qemu_detached "$version"

    evssim_wait_for_device /dev/nvme0n1
    evssim_wait_for_device /dev/nvme1n1

    # Run tests inside the guest
    echo "INFO Running filesystem mount test"
    set +e
    evssim_guest "cd ./guest; sudo EVSSIM_PROVISIONED_DEVICE_COUNT=$EVSSIM_PROVISIONED_DEVICE_COUNT nosetests -v -s filesystems"
    test_rc=$?
    set -e

    # Formats nvme0n1 again and traces exofs operations on it
    echo "INFO Running exofs test"
    OUTPUT_DIR="/tmp/output"
    set +e
    evssim_guest "sudo OUTPUT_DIR=$OUTPUT_DIR ./exofs/run_osd_emulator_and_mount_exofs.sh"
    exofs_rc=$?
    set -e

    # When debugging you can find trace logs at OUTPUT_DIR
    evssim_copy_from_guest $OUTPUT_DIR $OUTPUT_DIR

    # Stop qemu and wait
    evssim_qemu_flush_disk
    evssim_qemu_stop

    # Each device must have been simulated: the ELK logs tag every FTL event
    # with its device_index
    local logs index
    local ftl_rc=0
    mapfile -t logs < <(find "$EVSSIM_ROOT_PATH/$EVSSIM_LOGS_FOLDER" -maxdepth 1 -name 'elk_log_file-*.log' -newer "$marker")
    rm -f "$marker"
    for index in 0 1; do
        if [ ${#logs[@]} -gt 0 ] && grep -qsE "\"device_index\": $index[ ,}]" "${logs[@]}"; then
            echo "INFO FTL events present for device_index $index"
        else
            echo "ERROR No FTL events for device_index $index; the device was not simulated"
            ftl_rc=1
        fi
    done

    # Fail if tests fails
    if [ $test_rc -ne 0 ]; then
        echo "ERROR filesystem mount test failed"
        exit $test_rc
    fi
    if [ $exofs_rc -ne 0 ]; then
        echo "ERROR exofs test failed"
        exit $exofs_rc
    fi
    exit $ftl_rc
}

filesystems_test "$@"
