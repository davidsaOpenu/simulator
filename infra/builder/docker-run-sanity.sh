#!/bin/bash
source ./builder.sh

# Make a fresh copy
evssim_qemu_fresh_image

# Run qemu
evssim_qemu_detached


function check_evssim_guest_for_dev_nvme {
    is_dev_exists=0

    if evssim_guest ls -al /dev/nvme0n1 2>/dev/null >/dev/null; then
        echo "eVSSIM Up & Running!"
        is_dev_exists=1
    else
        echo "eVSSIM Failed to find /dev/nvme0n1."
    fi
    return $is_dev_exists
}

check_evssim_guest_for_dev_nvme
status=$?

if [[ $status == 0 ]]; then
    # Before checking if device nvme0n1 exists we should wait
    # Otherwise there are sporadic failures on "Failed to find device" error
    sleep 10
    echo "Check eVSSIM again.."
    check_evssim_guest_for_dev_nvme
    if [[ $? == 0 ]]; then
        exit 1
    fi
fi
