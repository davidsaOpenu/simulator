#!/bin/bash
source ./builder.sh

# Build the image
cd ./builder && docker build -t $EVSSIM_DOCKER_IMAGE_NAME .