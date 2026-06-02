#!/bin/bash
source ./builder.sh

evssim_validate_arguments "$0" "$#"
version="$1"

evssim_run_at_folder "$version" "open-osd" "make KSRC=/code/kernel ARCH=x86_64 clean && \
    make KSRC=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_KERNEL_FOLDER -j\`nproc\` && \
    cp usr/mkfs.exofs $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/mkfs.exofs && \
    cp lib/libosd.so $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/libosd.so"

# Copy OSD emulation and exofs setup script into the dist directory
evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/scripts" "cp -r exofs \
    $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/exofs"
