#!/bin/bash
source ./builder.sh

# Configure qemu
evssim_run_at_folder $EVSSIM_QEMU_FOLDER ./configure \
    --enable-trace-backends=log \
    --disable-docs --enable-tools \
    --enable-linux-aio \
    --enable-vssim \
    --disable-sdl --disable-gtk \
    --enable-kvm --target-list=x86_64-softmmu \
    "--extra-cflags='-Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations $COMPILATION_CFLAGS'"

# Make
evssim_run_at_folder $EVSSIM_QEMU_FOLDER make clean
evssim_run_at_folder $EVSSIM_QEMU_FOLDER bear -- make -j8

# Build osc-osd
evssim_run_at_folder "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd" "make clean && make -j\`nproc\` && \
    cp -r . $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/osc-osd"

# Build mkfs.exofs executable
evssim_run_at_folder "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/open-osd" "make KSRC=/code/kernel ARCH=x86_64 clean && \
    make KSRC=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_KERNEL_FOLDER -j\`nproc\` && \
    cp usr/mkfs.exofs $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/mkfs.exofs && \
    cp lib/libosd.so $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/libosd.so && \
    cp run_osd_emulator_and_mount_exofs.sh $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/run_osd_emulator_and_mount_exofs.sh"
