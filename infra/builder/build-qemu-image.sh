#!/bin/bash
source ./builder.sh

prog=$(basename "$0")
if [ "$#" -ne 2 ]; then
    echo $prog: Error: Wrong number of arguments.
    echo "Usage: $prog VM_VERSION CONTAINER_VERSION"
    echo "Builds a guest VM image based on VM_VERSION version, building it from inside a host docker container based on CONTAINER_VERSION version."
    exit 2
fi
vm_version="$1"
docker_tag="$2"

IMAGE_PATH=$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
IMAGE_PATH_TEMPLATE=$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE".template"

# Build the initial image
EVSSIM_RUN_SUDO=y evssim_run "$docker_tag" "/scripts/image-maker/${vm_version}.sh"
EVSSIM_RUN_SUDO=y evssim_run "$docker_tag" chown -R external:external $EVSSIM_DIST_FOLDER

# Make a clone
cp -f $IMAGE_PATH $IMAGE_PATH_TEMPLATE
