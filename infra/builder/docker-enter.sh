#!/bin/bash
source ./builder.sh

evssim_validate_arguments "$0" "$#"
version="$1"

DOCKER_ID=$(docker ps -f ancestor="$EVSSIM_DOCKER_IMAGE_NAME:$version" --format "{{.ID}}" -n 1)
if [ ! -z ${DOCKER_ID:-} ]; then
    docker exec -ti $DOCKER_ID bash
fi
