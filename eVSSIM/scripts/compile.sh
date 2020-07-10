#!/bin/bash

# compile osc-osd
cd eVSSIM/osc-osd/
make target || { echo 'make osc-ods target failed' ; exit 1; }
cd ../..

#compile host tests
cd eVSSIM/tests/host
make distclean || { echo 'eVSSIM/tests/host distclean failed' ; exit 1; }
make mklink || { echo 'eVSSIM/tests/host mklink failed' ; exit 1; }
make || { echo 'eVSSIM/tests/host make failed' ; exit 1; }

#compile QEMU
cd ../../QEMU
make distclean || { echo '../../QEMU make distclean failed' ; exit 1; }

./configure --enable-io-thread --enable-linux-aio --target-list=x86_64-softmmu --enable-sdl --enable-vssim --extra-cflags='-Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations -Wno-error=cpp' --disable-docs --cc=gcc-4.6 || { echo '../../QEMU configure failed' ; exit 1; }
make || { echo '../../QEMU make failed' ; exit 1; }
