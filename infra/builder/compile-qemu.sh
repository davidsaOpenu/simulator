#!/bin/bash
set -e

source ./builder.sh

UBUNTU_VERSION="$1"

# Setup git branch, and cleanup artifacts on host
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" fetch --all --prune
case "$UBUNTU_VERSION" in
	14.04)
		# Leave untagged target image to use and make sure we use correct branch for 14.04
		git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" checkout master
		# this option is not set for qemu 11.0 branch
		VSSIM_CONFIGURE_ARGS="--enable-vssim"
		;;
    	26.04)
		# Tag image target name as evssim:26.04 and use 11.0 branch for 26.04
		export EVSSIM_DOCKER_IMAGE_NAME="$EVSSIM_DOCKER_IMAGE_NAME:$UBUNTU_VERSION"
		echo "INFO switching qemu source tree to branch 11.0 for Ubuntu 26.04 build"
		git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" checkout 11.0
		#git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" reset --hard "$QEMU_REMOTE/11.0"
		;;
	*)
        	echo "ERROR unsupported qemu compile version: $UBUNTU_VERSION"
        	echo "Usage: $0 <14.04|26.04>"
        	exit 1
        ;;
esac

# Necessary since builds interfere with each other due to artifacts
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" pull --ff-only # !!!! THIS WILL FAIL FOR CHANGES IN THE QEMU BRANCH !!!!
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" reset --hard
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" clean -fdx
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" submodule update --init --recursive

echo "===== QEMU ====="
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" rev-parse HEAD
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" branch --show-current

echo "===== FTL HEADER ====="
grep -n "lookup_object" \
    "$EVSSIM_ROOT_PATH/simulator/eVSSIM/FTL_SOURCE/PAGE_MAP/ftl_obj_strategy.h" || true

echo "===== NVME CALLS ====="
grep -n "lookup_object" \
    "$EVSSIM_ROOT_PATH/qemu/hw/block/nvme.c" || true

# Configure qemu
evssim_run_at_folder $EVSSIM_QEMU_FOLDER ./configure \
    --enable-trace-backends=log \
    --disable-docs --enable-tools \
    --enable-linux-aio \
    --disable-sdl --disable-gtk \
    --enable-kvm --target-list=x86_64-softmmu \
    $VSSIM_CONFIGURE_ARGS \
    "--extra-cflags='-Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations $COMPILATION_CFLAGS'"

# Make
if [ "$UBUNTU_VERSION" = "26.04" ]; then
    # Note: build with ninja for 26.04
    evssim_run_at_folder "$EVSSIM_QEMU_FOLDER" bear -- ninja -C build -j8
    echo "INFO 26.04 QEMU build complete. Skipping legacy open-osd/exofs build."
    exit 0
fi

evssim_run_at_folder $EVSSIM_QEMU_FOLDER make clean
evssim_run_at_folder $EVSSIM_QEMU_FOLDER bear -- make -j8

# Build osc-osd
evssim_run_at_folder "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd" "make MK_PATH=. ARCH=x86_64 clean && \
    make MK_PATH=. ARCH=x86_64 -j\`nproc\` && \
    rsync -av --progress --exclude='.git' ./ $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/osc-osd/"

# Build mkfs.exofs executable and the shared lib libosd (required by mkfs.exofs)
evssim_run_at_folder "$EVSSIM_SIMULATOR_FOLDER/" "git submodule update --init --recursive && \
    git submodule foreach --recursive 'git reset --hard && git clean -fdx'"

evssim_run_at_folder "open-osd" "make KSRC=/code/kernel ARCH=x86_64 clean && \
    make KSRC=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_KERNEL_FOLDER -j\`nproc\` && \
    cp usr/mkfs.exofs $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/mkfs.exofs && \
    cp lib/libosd.so $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/libosd.so"

# Copy OSD emulation and exofs setup script into the dist directory
evssim_run_at_folder "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/scripts" "cp -r exofs \
    $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/exofs"
