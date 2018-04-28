#!/bin/bash
source ./builder.sh

# Build the image
cd $EVSSIM_BUILDER_FOLDER && docker build -t $EVSSIM_DOCKER_IMAGE_NAME .