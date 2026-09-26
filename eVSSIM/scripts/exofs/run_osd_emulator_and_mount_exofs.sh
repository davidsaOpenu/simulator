#!/bin/bash
set -e

OUTPUT_DIR=${OUTPUT_DIR:-/tmp/output}

# Check if the script is being run as root
if [[ $EUID -ne 0 ]]; then
  echo "Please execute 'sudo su' before executing this script."
  exit 1
fi

NVME_DEV=/dev/nvme0n1
MOUNT_POINT=/mnt/exofs0
TEST_UTIL_DIR=/home/esd/exofs/tracing_the_kernel/lab

# Creating client
pushd /home/esd/exofs/tracing_the_kernel/lab && make && popd

source "$(dirname "$0")/exofs_lib.sh"
setup_exofs

# setup mounts already; the traced suite unmounts MOUNT_POINT before its own mount
export MOUNT_POINT
cd $TEST_UTIL_DIR
sudo -E $TEST_UTIL_DIR/test_and_log_all_operations.sh

echo "> Done!"

magic_number=$(stat -fc '%t' $MOUNT_POINT)

if [[ $magic_number == "5df5" ]]; then
    echo "The magic number is valid."
else
    echo "The magic number is invalid. It is 0x$magic_number instead of 0x5df5."
    exit 1
fi
