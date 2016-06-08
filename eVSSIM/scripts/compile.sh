#!/bin/bash

cd eVSSIM/MONITOR/SSD_MONITOR_PM
qmake -o Makefile ssd_monitor_p.pro
make
cd ../../QEMU
./configure --enable-io-thread --enable-linux-aio --target-list=x86_64-softmmu --enable-sdl --enable-vssim --extra-cflags='-Wno-deprecated-declarations -Wno-unused-but-set-variable'
make
