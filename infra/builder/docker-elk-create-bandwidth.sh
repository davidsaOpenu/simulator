#!/bin/bash
source ./builder.sh

# Make a fresh copy
evssim_qemu_fresh_image

# Run qemu with test specific configuration
evssim_qemu_detached

# Run tests inside the guest
set +e
evssim_guest "cd ./guest/elk_fio; sudo VSSIM_NEXTGEN_BUILD_SYSTEM=1 ./run_fio.sh; sleep 4"
test_rc=$?
set -e

# Stop qemu and wait
evssim_qemu_flush_disk
evssim_qemu_stop

# Fail if tests fails
if [ $test_rc -ne 0 ]; then
    echo "ERROR Failed creating bandwidth for ELK logs, error=$test_rc"
    exit $test_rc
fi
