#!/bin/bash
source ./builder.sh

# Configure qemu
evssim_run_at_folder $EVSSIM_QEMU_FOLDER ./configure \
    --enable-io-thread --enable-linux-aio --target-list=x86_64-softmmu --enable-sdl --enable-vssim \
    "--extra-cflags='-Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations $COMPILATION_CFLAGS'" \
    --cc=$COMPILATION_GCC --enable-xen --disable-docs

# Make
evssim_run_at_folder $EVSSIM_QEMU_FOLDER bear make
