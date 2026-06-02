#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "$#"
version="$1"

TOOLS_PATH="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_TOOLS_FOLDER"

# Prepare directory structure
evssim_run_at_folder "$version" "$EVSSIM_TOOLS_FOLDER" "\
    mkdir -p usr/local/{sbin,lib} && \
    mkdir -p '$EVSSIM_GUEST_HOME_PATH/'{osc-osd,exofs} \
"

# Build osc-osd
evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd" "\
    make MK_PATH=. ARCH=x86_64 clean && \
    make MK_PATH=. ARCH=x86_64 -j\`nproc\` && \
    rsync -av --progress --exclude='.git' ./ '$TOOLS_PATH/$EVSSIM_GUEST_HOME_PATH/osc-osd/' \
"

# Build mkfs.exofs executable and the shared lib libosd (required by mkfs.exofs)
evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/" "\
    git submodule update --init --recursive && \
    git submodule foreach --recursive 'git reset --hard && git clean -fdx' \
"

evssim_run_at_folder "$version" "open-osd" "\
    make KSRC='$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_KERNEL_FOLDER' ARCH=x86_64 clean && \
    make KSRC='$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_KERNEL_FOLDER' -j\`nproc\` && \
    cp usr/mkfs.exofs '$TOOLS_PATH/usr/local/sbin/mkfs.exofs' && \
    cp lib/libosd.so '$TOOLS_PATH/usr/local/lib/libosd.so' \
"

# Copy OSD emulation and exofs setup script into the dist directory
evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/scripts" \
    "cp -r exofs '$TOOLS/$EVSSIM_GUEST_HOME_PATH/'"
