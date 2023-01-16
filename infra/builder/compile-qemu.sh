#!/bin/bash
source ./builder.sh

# Add json to configure
if ! grep -q 'QEMU_CFLAGS="-I/usr/include/json-c/ -D__STDC_VERSION__=0 $QEMU_CFLAGS"' $EVSSIM_QEMU_FOLDER/configure && ! grep -q 'LIBS="-ljson-c $LIBS"' $EVSSIM_QEMU_FOLDER/configure 
then
qemu_vssim_line=$(grep -n 'if test "$vssim" = "yes" ; then' $EVSSIM_QEMU_FOLDER/configure | cut -f1 -d: | head -1)
sed -i "${qemu_vssim_line}"'a \\tQEMU_CFLAGS="-I/usr/include/json-c/ -D__STDC_VERSION__=0 $QEMU_CFLAGS"\n\tLIBS="-ljson-c $LIBS"' $EVSSIM_QEMU_FOLDER/configure > /dev/null
fi

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
