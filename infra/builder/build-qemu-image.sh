#!/bin/bash
source ./builder.sh

IMAGE_PATH=$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
IMAGE_PATH_TEMPLATE=$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE".template"

# User template if we have one
if [ -e $IMAGE_PATH_TEMPLATE ]; then
    echo WARNING Image file already exists $IMAGE_PATH. Will use template.

    cp -f $IMAGE_PATH_TEMPLATE $IMAGE_PATH
else
    # Build the initial image
    EVSSIM_RUN_SUDO=y evssim_run "/scripts/image-maker.sh"
    EVSSIM_RUN_SUDO=y evssim_run chown -R $UID:$UID $EVSSIM_DIST_FOLDER

    # Make a clone
    cp -f $IMAGE_PATH $IMAGE_PATH_TEMPLATE
fi