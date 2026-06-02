#!/bin/bash

TOOLS_PATH="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_IMAGE_TOOLS_FOLDER"
LOCAL_NVME_CLI_PATH="$EVSSIM_ROOT_PATH/$EVSSIM_NVME_CLI_FOLDER"
TARGET_BRANCH="master"

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

evssim_run_at_folder ubuntu-14.04 "$EVSSIM_NVME_CLI_FOLDER" "\
    make clean && \
    make -j$(nproc) && \
    mkdir -p '$TOOLS_PATH/$EVSSIM_GUEST_HOME_PATH/usr/local/sbin' && \
    cp nvme '$TOOLS_PATH/$EVSSIM_GUEST_HOME_PATH/usr/local/sbin/nvme'"
