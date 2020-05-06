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
evssim_run_at_folder $EVSSIM_QEMU_FOLDER bear make
