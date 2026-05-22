#!/bin/bash
source ./builder.sh

version=${EVSSIM_KERNEL_COMPILE_CONTAINER#ubuntu:}
export EVSSIM_DOCKER_IMAGE_NAME="$EVSSIM_DOCKER_IMAGE_NAME:$version"

# Build kernel parts and save into the dist folder
rm -rf $EVSSIM_DIST_FOLDER/kernel
mkdir -p $EVSSIM_DIST_FOLDER/kernel

install_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/
initrd_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/initrd.img-$EVSSIM_KERNEL_DIST

CURRENT_KERNEL_HASH=$(evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "git rev-parse HEAD")
TARGET_KERNEL_HASH=$(evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "git rev-parse $EVSSIM_KERNEL_BRANCH")

if [[ "$CURRENT_KERNEL_HASH" != "$TARGET_KERNEL_HASH" ]]; then
	GIT_CLEAN_RESET="git clean -fdx && git reset --hard HEAD"
    evssim_run_at_folder $EVSSIM_KERNEL_FOLDER $GIT_CLEAN_RESET
	evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "git checkout $EVSSIM_KERNEL_BRANCH"
	evssim_run_at_folder $EVSSIM_KERNEL_FOLDER $GIT_CLEAN_RESET
else
    echo "Already on '$EVSSIM_KERNEL_BRANCH', skipping clean."
fi

KERNEL_VERSIONS_FOLDER="$EVSSIM_VERSIONS_KERNEL_FOLDER/$EVSSIM_KERNEL_VERSION"
KERNEL_VERSION_CONFIG_FILE="$KERNEL_VERSIONS_FOLDER/.config"
DOCKER_KERNEL_VERSION_CONFIG_FILE="$EVSSIM_DOCKER_ROOT_PATH/$KERNEL_VERSION_CONFIG_FILE"

if ! evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "diff -q $DOCKER_KERNEL_VERSION_CONFIG_FILE .config" >/dev/null; then
	echo "Config files differ, copying to kernel folder"
	evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "cp $DOCKER_KERNEL_VERSION_CONFIG_FILE ."
fi

EVSSIM_KERNEL_MAKE="make $EVSSIM_KERNEL_MAKE_ARGS KERNELRELEASE=$EVSSIM_KERNEL_DIST -j\`nproc\` "
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "$EVSSIM_KERNEL_MAKE all"
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "$EVSSIM_KERNEL_MAKE INSTALL_PATH=$install_path INSTALL_MOD_PATH=$install_path modules_install install"
EVSSIM_RUN_SUDO=y evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "$EVSSIM_KERNEL_MAKE modules_install install && mkinitramfs -o $initrd_path $EVSSIM_KERNEL_DIST"
EVSSIM_RUN_SUDO=y evssim_run_at_folder $EVSSIM_KERNEL_FOLDER chown -R external:external $initrd_path
