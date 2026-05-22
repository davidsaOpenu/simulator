#!/bin/bash
source ./builder.sh

version=${EVSSIM_KERNEL_COMPILE_CONTAINER#ubuntu:}
export EVSSIM_DOCKER_IMAGE_NAME="$EVSSIM_DOCKER_IMAGE_NAME:$version"

# Build kernel parts and save into the dist folder
rm -rf $EVSSIM_DIST_FOLDER/kernel
mkdir -p $EVSSIM_DIST_FOLDER/kernel

install_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/
initrd_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/initrd.img-$EVSSIM_KERNEL_DIST

GIT_CLEAN_RESET="git clean -fdx && git reset --hard HEAD"

evssim_run_at_folder $EVSSIM_KERNEL_FOLDER $GIT_CLEAN_RESET
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "git checkout $EVSSIM_KERNEL_BRANCH"
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER $GIT_CLEAN_RESET
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make KERNELRELEASE=$EVSSIM_KERNEL_DIST $EVSSIM_KCONFIG defconfig"
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make KERNELRELEASE=$EVSSIM_KERNEL_DIST -j\`nproc\`"
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make KERNELRELEASE=$EVSSIM_KERNEL_DIST modules -j\`nproc\`"
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make KERNELRELEASE=$EVSSIM_KERNEL_DIST INSTALL_PATH=$install_path INSTALL_MOD_PATH=$install_path modules_install install"
EVSSIM_RUN_SUDO=y evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make KERNELRELEASE=$EVSSIM_KERNEL_DIST modules_install install && mkinitramfs -o $initrd_path $EVSSIM_KERNEL_DIST"
EVSSIM_RUN_SUDO=y evssim_run_at_folder $EVSSIM_KERNEL_FOLDER chown -R external:external $initrd_path
