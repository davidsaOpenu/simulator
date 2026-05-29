#!/bin/bash
set -e

source ./builder.sh

# Require explicit version when calling the script
UBUNTU_VERSION="$1"
case "$UBUNTU_VERSION" in
	14.04|26.04)
		;;
	*)
	echo "ERROR unsupported qemu compile version: $UBUNTU_VERSION"
	echo "Usage: $0 [14.04|26.04]"
	exit 1
	;;
esac

DOCKERFILE="$EVSSIM_BUILDER_FOLDER/versions/containers/$UBUNTU_VERSION/Dockerfile"
docker build \
	-t "$EVSSIM_DOCKER_IMAGE_NAME:$UBUNTU_VERSION" \
	-f "$DOCKERFILE" \
	"$EVSSIM_BUILDER_FOLDER"

if [ "$UBUNTU_VERSION" = "14.04" ]; then
    docker tag "$EVSSIM_DOCKER_IMAGE_NAME:14.04" "$EVSSIM_DOCKER_IMAGE_NAME"
fi
