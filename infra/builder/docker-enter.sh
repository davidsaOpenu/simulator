#!/bin/bash
source ./builder.sh

DOCKER_ID=$(docker ps -f ancestor=$EVSSIM_DOCKER_IMAGE_NAME --format "{{.ID}}" -n 1)
if [ ! -z $DOCKER_ID ]; then
    docker exec -ti $DOCKER_ID bash
fi
