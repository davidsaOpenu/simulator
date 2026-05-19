#!/bin/bash
set -e

source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

# Setup git branch, and cleanup artifacts on host
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" fetch --all --prune
case "$version" in
	"ubuntu-14.04")
		# Make sure we use correct branch for 14.04
		git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" checkout master
		# this option is not set for qemu 11.0 branch
		VSSIM_CONFIGURE_ARGS="--enable-vssim"
		;;
	"ubuntu-26.04")
		# Use 11.0 branch for 26.04
		echo "INFO switching qemu source tree to branch 11.0 for Ubuntu 26.04 build"
		git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" checkout 11.0
		# no --enable-vssim option for 11.0 branch
		VSSIM_CONFIGURE_ARGS=""
		;;
	*)
        	echo "ERROR unsupported qemu compile version: $version"
        	echo "Usage: $0 <ubuntu-14.04|ubuntu-26.04>"
        	exit 1
        ;;
esac

# Necessary since builds interfere with each other due to artifacts
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" pull --ff-only # !!!! THIS WILL FAIL FOR CHANGES IN THE QEMU BRANCH !!!!
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" reset --hard
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" clean -fdx
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" submodule update --init --recursive

# Configure qemu
evssim_run_at_folder "$version" $EVSSIM_QEMU_FOLDER ./configure \
    --enable-trace-backends=log \
    --disable-docs --enable-tools \
    --enable-linux-aio \
    --disable-sdl --disable-gtk \
    --enable-kvm --target-list=x86_64-softmmu \
    $VSSIM_CONFIGURE_ARGS \
    "--extra-cflags='-Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations -Wno-error=cpp'"

# Make
if [ "$version" = "ubuntu-26.04" ]; then
    # Note: build with ninja for 26.04
    evssim_run_at_folder "$version" "$EVSSIM_QEMU_FOLDER" bear -- ninja -C build -j$(nproc)
    echo "INFO 26.04 QEMU build complete. Skipping legacy open-osd/exofs build."
    exit 0
fi

evssim_run_at_folder "$version" $EVSSIM_QEMU_FOLDER make clean
evssim_run_at_folder "$version" $EVSSIM_QEMU_FOLDER bear -- make -j$(nproc)

# Build osc-osd
evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd" "make MK_PATH=. ARCH=x86_64 clean && \
    make MK_PATH=. ARCH=x86_64 -j\`nproc\` && \
    rsync -av --progress --exclude='.git' ./ $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/osc-osd/"

# Build mkfs.exofs executable and the shared lib libosd (required by mkfs.exofs)
evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/" "git submodule update --init --recursive && \
    git submodule foreach --recursive 'git reset --hard && git clean -fdx'"

evssim_run_at_folder "$version" "open-osd" "make KSRC=/code/kernel ARCH=x86_64 clean && \
    make KSRC=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_KERNEL_FOLDER -j\`nproc\` && \
    cp usr/mkfs.exofs $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/mkfs.exofs && \
    cp lib/libosd.so $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/libosd.so"

# Copy OSD emulation and exofs setup script into the dist directory
evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/scripts" "cp -r exofs \
    $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/exofs"
