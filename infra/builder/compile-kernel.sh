#!/bin/bash
source ./builder.sh

# Build kernel parts and save into the dist folder
rm -rf $EVSSIM_DIST_FOLDER/kernel
mkdir -p $EVSSIM_DIST_FOLDER/kernel

install_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/
initrd_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/initrd.img-$EVSSIM_KERNEL_DIST

evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make $EVSSIM_KCONFIG -j\`nproc\`"
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make $EVSSIM_KCONFIG modules -j\`nproc\`"
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make $EVSSIM_KCONFIG INSTALL_PATH=$install_path INSTALL_MOD_PATH=$install_path modules_install install"
EVSSIM_RUN_SUDO=y evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make modules_install install && mkinitramfs -o $initrd_path $EVSSIM_KERNEL_DIST"
EVSSIM_RUN_SUDO=y evssim_run_at_folder $EVSSIM_KERNEL_FOLDER chown -R external:external $initrd_path

