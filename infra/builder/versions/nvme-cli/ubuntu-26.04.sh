#!/bin/bash

TOOLS_PATH="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_IMAGE_TOOLS_FOLDER"
LOCAL_NVME_CLI_PATH="$EVSSIM_ROOT_PATH/$EVSSIM_NVME_CLI_FOLDER"
TARGET_BRANCH="3.0-a.5"

echo Switching nvme-cli branch to $TARGET_BRANCH...
if [[ "$(git -C "$LOCAL_NVME_CLI_PATH" branch --show-current)" = "$TARGET_BRANCH" ]]; then
    echo Already on $TARGET_BRANCH branch. Skipping.
# Check if the repo has unsaved changes
elif [[ -n "$(git -C "$LOCAL_NVME_CLI_PATH" status --porcelain)" ]]; then
    echo ERROR: nvme-cli has unsaved changes. Commit, Stash, or Reset changes and try again
    exit 1
else
    git -C "$LOCAL_NVME_CLI_PATH" switch "$TARGET_BRANCH"
fi

evssim_run_at_folder ubuntu-26.04 "$EVSSIM_NVME_CLI_FOLDER" "\
    make clean && \
    make -j$(nproc) && \
    mkdir -p '$TOOLS_PATH/usr/local'/{sbin,include/nvme,lib/x86_64-linux-gnu/pkgconfig} && \
    cp .build/libnvme/src/libnvme.so.3.0.0 '$TOOLS_PATH/usr/local/lib/x86_64-linux-gnu/' && \
    cp .build/nvme '$TOOLS_PATH/usr/local/sbin' && \
    cp libnvme/src/nvme/*.h '$TOOLS_PATH/usr/local/include/nvme' && \
    cp libnvme/src/libnvme-mi.h '$TOOLS_PATH/usr/local/include' && \
    cp .build/libnvme/src/libnvme.h '$TOOLS_PATH/usr/local/include' && \
    cp .build/meson-private/libnvme.pc '$TOOLS_PATH/usr/local/lib/x86_64-linux-gnu/pkgconfig' && \
    ln -sf libnvme.so.3.0.0 '$TOOLS_PATH/usr/local/lib/x86_64-linux-gnu/libnvme.so.3' && \
    ln -sf libnvme.so.3 '$TOOLS_PATH/usr/local/lib/x86_64-linux-gnu/libnvme.so'"
