#!/bin/bash -x
source ./builder.sh

IMAGE_PATH=$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
IMAGE_PATH_TEMPLATE=$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE".template"

# Build the initial image
# EVSSIM_RUN_SUDO=y evssim_run "/scripts/image-maker.sh"
# EVSSIM_RUN_SUDO=y evssim_run chown -R external:external $EVSSIM_DIST_FOLDER
evssim_run "/scripts/image-maker.sh"

# Make a clone to copy from, ensuring a clean image for future use
cp -f $IMAGE_PATH $IMAGE_PATH_TEMPLATE
