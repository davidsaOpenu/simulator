#!/bin/bash
source ./builder.sh

# Build the image

cd $EVSSIM_BUILDER_FOLDER
for folder in $EVSSIM_ROOT_PATH/$EVSSIM_CONTAINERS_FOLDER/*; do
	version=$(basename "$folder")
	docker build -t "$EVSSIM_DOCKER_IMAGE_NAME:$version" -f "$folder/Dockerfile" .
done
