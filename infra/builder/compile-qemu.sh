#!/bin/bash
source ./builder.sh

# Compile osd
evssim_run_at_folder $EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd make target

# Configure qemu
#evssim_run_at_folder $EVSSIM_QEMU_FOLDER ./configure \
#    --enable-io-thread --enable-linux-aio --target-list=x86_64-softmmu --enable-sdl --enable-vssim \
#    "--extra-cflags='-Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations $COMPILATION_CFLAGS'" \
#    --cc=$COMPILATION_GCC --enable-xen --disable-docs

#evssim_run_at_folder $EVSSIM_QEMU_FOLDER bash
evssim_run_at_folder $EVSSIM_QEMU_FOLDER ./configure \
    --enable-trace-backends=log \
    --disable-git-update --disable-docs --enable-tools \
    --enable-linux-aio \
    --disable-sdl --disable-gtk \
    --enable-kvm --target-list=x86_64-softmmu \
    "--extra-cflags='-Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations $COMPILATION_CFLAGS'"

# Make
evssim_run_at_folder $EVSSIM_QEMU_FOLDER bear make
