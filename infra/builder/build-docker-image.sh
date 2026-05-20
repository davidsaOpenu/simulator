#!/bin/bash
source ./builder.sh

set -e

# Build the image

cd $EVSSIM_BUILDER_FOLDER
for folder in $EVSSIM_CONTAINERS_FOLDER/*; do
	version=$(basename "$folder")
	docker build -t "$EVSSIM_DOCKER_IMAGE_NAME:$version" -f "$folder/Dockerfile" .
done
