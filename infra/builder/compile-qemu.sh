#!/bin/bash
set -e

# S2b resolves source/profile inputs without builder.sh side effects or a build.
if [ "${1:-}" = "--resolve-dependencies" ]; then
    exec python3 "$(dirname "$(realpath "$0")")/qemu-inputs.py" "$@"
fi

# A container is a build environment, not a source-branch selector. Reject
# conflicting callers before builder.sh or any checkout/cleanup side effects.
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <ubuntu-14.04|ubuntu-26.04>" >&2
    exit 1
fi
version="${1:-}"
if [[ -z "${EVSSIM_QEMU_BRANCH:-}" || -z "${EVSSIM_QEMU_COMPILE_CONTAINER:-}" ]]; then
    echo "ERROR: Load the selected configuration with source ./env.sh first" >&2
    exit 1
fi
if [[ "$version" != "$EVSSIM_QEMU_COMPILE_CONTAINER" ]]; then
    echo "ERROR: Requested container $version conflicts with selected profile $EVSSIM_VERSIONS_CONFIGURATION_ID ($EVSSIM_QEMU_COMPILE_CONTAINER)" >&2
    exit 1
fi
if [[ "$version" != ubuntu-14.04 && "$version" != ubuntu-26.04 ]]; then
    echo "ERROR: Unsupported QEMU compile container: $version" >&2
    exit 1
fi
git check-ref-format --branch "$EVSSIM_QEMU_BRANCH" >/dev/null
# Compiling a frozen artifact record is S2c work. Reject it here rather than
# silently building from different inputs through the existing version path.
if [[ -n "${EVSSIM_QEMU_INPUT_RECORD:-}" ]]; then
    echo "ERROR: Saved-input compilation belongs to S2c; no build was started" >&2
    exit 1
fi

source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"

evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd" "make MK_PATH=. ARCH=x86_64 clean"

# Setup git branch, and cleanup artifacts on host
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" fetch --all --prune
echo "INFO building QEMU branch $EVSSIM_QEMU_BRANCH, profile $EVSSIM_VERSIONS_CONFIGURATION_ID, container $version"
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" checkout "$EVSSIM_QEMU_BRANCH"
case "$version" in
	"ubuntu-14.04")
		VSSIM_CONFIGURE_ARGS="--enable-vssim --extra-cflags='-Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations -Wno-error=cpp'"
		;;
	"ubuntu-26.04")
		VSSIM_CONFIGURE_ARGS="--enable-werror"
		;;
	*)
        	echo "ERROR unsupported qemu compile version: $version"
        	echo "Usage: $0 <ubuntu-14.04|ubuntu-26.04>"
        	exit 1
        ;;
esac

# Necessary since builds interfere with each other due to artifacts
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" pull --ff-only
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" reset --hard
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" clean -fdx
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" submodule update --init --recursive

if [ "$version" = "ubuntu-26.04" ]; then
    "$EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/infra/builder/prepare-qemu-vssim.sh" \
        "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" \
        "$EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM"
fi

# Configure qemu
evssim_run_at_folder "$version" $EVSSIM_QEMU_FOLDER ./configure \
    --enable-trace-backends=log \
    --disable-docs --enable-tools \
    --enable-linux-aio \
    --disable-sdl --disable-gtk \
    --enable-kvm --target-list=x86_64-softmmu \
    $VSSIM_CONFIGURE_ARGS

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
