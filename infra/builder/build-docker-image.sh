#!/bin/bash
source ./builder.sh

docker_network_args=()
read -r -a docker_network_args <<< "$(evssim_docker_network_args)"

# Build the image
cd $EVSSIM_BUILDER_FOLDER && docker build "${docker_network_args[@]}" -t $EVSSIM_DOCKER_IMAGE_NAME -f $EVSSIM_COMPILE_DOCKERFILE .
