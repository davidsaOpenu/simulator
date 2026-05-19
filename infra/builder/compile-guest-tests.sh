#!/bin/bash
source ./builder.sh

version=${EVSSIM_GUEST_TESTS_COMPILE_CONTAINER#ubuntu:}
export EVSSIM_DOCKER_IMAGE_NAME="$EVSSIM_DOCKER_IMAGE_NAME:$version"

# Build nvme
evssim_run_at_folder $EVSSIM_NVME_CLI_FOLDER "make clean && make && cp nvme $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/nvme"

# Build dnvme & tnvme
evssim_run_at_folder $EVSSIM_NVME_COMPLIANCE_FOLDER "cd dnvme && make DIST=$EVSSIM_KERNEL_DIST KDIR=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/lib/modules/$EVSSIM_KERNEL_DIST/build && cp dnvme.ko $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/dnvme.ko"
evssim_run_at_folder $EVSSIM_NVME_COMPLIANCE_FOLDER "cd tnvme && make && cp tnvme $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/tnvme"
